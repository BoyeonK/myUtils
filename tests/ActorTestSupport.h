#pragma once

// Actor Runtime 테스트가 공유하는 관측 도구와 Actor 구현.
//
// 런타임이 프로세스 전역 단일이고 1회 기동·1회 종료라, 한 실행 파일은 worker 수
// 하나와 종료 한 번만 가질 수 있다. 그래서 테스트를 "필요한 런타임 형상"별로
// 실행 파일에 나눠 담는다. 어느 invariant가 어느 파일로 갔는지는
// design/actor-runtime.md §10 의 표에 있다.
//
// [주의] LiveActorCount()는 런타임 전역 누적값이다. 각 테스트는 자기 액터를
// 정리하고 기준선으로 돌아오는지 본다 — 0이 되기를 기다리면 앞선 테스트가 남긴
// 액터 때문에 영원히 기다린다.

#include "MyUtils/Actor.h"
#include "MyUtils/Runtime.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

inline int GFailCount = 0;
inline int GCheckCount = 0;

#define CHECK(expr)                                                        \
    do {                                                                   \
        ++GCheckCount;                                                     \
        if (!(expr)) {                                                     \
            std::printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
            ++GFailCount;                                                  \
        }                                                                  \
    } while (0)

inline void Section(const char* name) { std::printf("[%s]\n", name); }

template <class Pred>
inline bool WaitUntil(Pred pred, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

// 살아 있는 액터 수가 기준선으로 돌아오기를 기다린다.
inline bool WaitForLiveActors(std::size_t expected, int timeoutMs = 5000) {
    return WaitUntil([&] { return MyUtils::Actors::LiveActorCount() == expected; }, timeoutMs);
}

// ---------------------------------------------------------------------------
// 공용 메시지 / 관측 객체
//
// ACB가 Actor를 unique 소유하고 Finalize에서 파괴하므로, 테스트는
// Actor 내부를 직접 들여다볼 수 없다. 관측값은 외부 객체에 기록한다.
// ---------------------------------------------------------------------------

struct Stats {
    std::atomic<int> handled{ 0 };
    std::atomic<int> concurrentViolations{ 0 };
    std::atomic<int> inFlight{ 0 };
    std::atomic<int> sum{ 0 };

    std::mutex threadIdLock;
    std::set<std::thread::id> threadIds;
};

using StatsPtr = std::shared_ptr<Stats>;

struct IntMessage : MyUtils::Actors::Message {
    explicit IntMessage(int v) : value(v) {}
    int value;
};

// ---------------------------------------------------------------------------

namespace TestActors {

using MyUtils::Actors::Actor;
using MyUtils::Actors::ActorRef;
using MyUtils::Actors::Message;

class CountingActor : public Actor {
public:
    // handleDelayMs는 소비를 느리게 만들어 "처리되지 않은 일이 남아 있는 상태"를
    // 결정적으로 만들기 위한 것이다. 기본값 0이면 지연이 없다.
    explicit CountingActor(StatsPtr stats, int handleDelayMs = 0)
        : _stats(std::move(stats)), _handleDelayMs(handleDelayMs) {
    }

    void Handle(Message& message) override {
        // 같은 Actor가 동시에 두 worker에서 실행되면 여기서 잡힌다
        if (_stats->inFlight.fetch_add(1) != 0)
            _stats->concurrentViolations.fetch_add(1);

        if (auto* m = dynamic_cast<IntMessage*>(&message))
            _stats->sum.fetch_add(m->value);

        if (_handleDelayMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(_handleDelayMs));
        else
            std::this_thread::yield();   // 겹칠 틈을 넓혀 경합을 드러낸다

        _stats->inFlight.fetch_sub(1);
        _stats->handled.fetch_add(1);
    }

private:
    StatsPtr _stats;
    int _handleDelayMs;
};

class ThreadRecordingActor : public Actor {
public:
    explicit ThreadRecordingActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message&) override {
        {
            std::lock_guard<std::mutex> guard(_stats->threadIdLock);
            _stats->threadIds.insert(std::this_thread::get_id());
        }
        _stats->handled.fetch_add(1);
    }

private:
    StatsPtr _stats;
};

class ThrowingActor : public Actor {
public:
    explicit ThrowingActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message&) override {
        _stats->handled.fetch_add(1);
        throw std::runtime_error("handler failure");
    }

private:
    StatsPtr _stats;
};

// self-send: 메시지가 자기 자신에 대한 ActorRef를 들고 다닌다
struct ChainMessage : Message {
    ChainMessage(ActorRef s, int d) : self(std::move(s)), depth(d) {}
    ActorRef self;
    int depth;
};

class ChainActor : public Actor {
public:
    explicit ChainActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message& message) override {
        auto* m = dynamic_cast<ChainMessage*>(&message);
        if (m == nullptr)
            return;

        _stats->handled.fetch_add(1);

        // [L1 검증] mailbox 락을 쥔 채 Handle을 부르면 여기서 데드락한다
        if (m->depth > 0)
            m->self.Send(std::make_unique<ChainMessage>(m->self, m->depth - 1));
    }

private:
    StatsPtr _stats;
};

// self-stop.
// pendingBeforeStop > 0 이면 Stop 직전에 자기 자신에게 그만큼 보낸다. Stop 시점에
// 메일박스가 비어 있지 않은 상황을 "결정적으로" 만들기 위한 것이다 — 밖에서
// 두 건을 연달아 Send하면 worker가 첫 건을 먼저 집어갈 수 있어 flaky해진다.
struct StopSelfMessage : Message {
    StopSelfMessage(ActorRef s, int pending = 0)
        : self(std::move(s)), pendingBeforeStop(pending) {}
    ActorRef self;
    int pendingBeforeStop;
};

class SelfStoppingActor : public Actor {
public:
    explicit SelfStoppingActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message& message) override {
        _stats->handled.fetch_add(1);

        auto* m = dynamic_cast<StopSelfMessage*>(&message);
        if (m == nullptr)
            return;

        // Stop보다 먼저 보내므로 state가 아직 RUNNING이라 전부 accept된다.
        // 반환 시점에 메일박스에 확실히 쌓여 있으므로, worker가 Handle 반환 후
        // 상태를 재확인하지 않으면 이것들이 그대로 실행된다.
        // (CHECK는 스레드 안전하지 않아 여기서 쓰지 않는다. atomic에 기록한다)
        for (int i = 0; i < m->pendingBeforeStop; ++i) {
            if (m->self.Send(std::make_unique<IntMessage>(1)))
                _stats->sum.fetch_add(1);
        }

        m->self.Stop();   // [L1 검증] 같은 mailbox 락을 쓰므로 락 보유 시 데드락
    }

private:
    StatsPtr _stats;
};

// handler 안에서 Spawn.
//
// childOut은 테스트가 소유한다. 메시지는 Handle 반환 직후 파괴되므로 결과를
// 메시지 안에 두면 읽을 수 없다.
struct SpawnMessage : Message {
    SpawnMessage(StatsPtr childStats, std::shared_ptr<ActorRef> out)
        : stats(std::move(childStats)), childOut(std::move(out)) {}
    StatsPtr stats;
    std::shared_ptr<ActorRef> childOut;
};

class SpawningActor : public Actor {
public:
    explicit SpawningActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message& message) override {
        auto* m = dynamic_cast<SpawnMessage*>(&message);
        if (m == nullptr)
            return;

        // [L1 검증] registry 락을 잡는다. mailbox 락과 중첩되면 문제가 된다.
        // (CHECK는 스레드 안전하지 않아 여기서 쓰지 않는다. 유효성은 sum에 남긴다)
        ActorRef child = MyUtils::Actors::Spawn(std::make_unique<CountingActor>(m->stats));
        _stats->sum.fetch_add(child.IsValid() ? 1 : 0);
        child.Send(std::make_unique<IntMessage>(7));

        // 자식 핸들을 테스트에 넘긴다.
        *m->childOut = child;

        // [필수] handled 증가가 마지막이다. 테스트는 이 값을 보고 childOut을
        // 읽으므로, 먼저 올리면 아직 쓰이지 않은 핸들을 읽을 수 있다.
        _stats->handled.fetch_add(1);
    }

private:
    StatsPtr _stats;
};

// ---------------------------------------------------------------------------
// 스케줄러 라우팅 테스트용
// ---------------------------------------------------------------------------

// handler 안에서 여러 액터에게 뿌린다. worker 스레드에서 Send하므로 만들어지는
// task가 전부 그 worker의 local deque로 들어간다.
struct FanOutMessage : Message {
    explicit FanOutMessage(std::vector<ActorRef> t) : targets(std::move(t)) {}

    std::vector<ActorRef> targets;

    // 둘 다 선택 사항이다. 지정하면 fan-out 직후 fannedOut을 세우고 release가
    // 설 때까지 handler 안에서 대기한다 — local deque에 일이 쌓인 상태를
    // 타이밍에 기대지 않고 만들기 위한 것이다.
    std::atomic<bool>* fannedOut = nullptr;
    std::atomic<bool>* release = nullptr;
};

class FanOutActor : public Actor {
public:
    void Handle(Message& message) override {
        auto* m = dynamic_cast<FanOutMessage*>(&message);
        if (m == nullptr)
            return;

        // worker 위에서 도는 Send라 전부 이 worker의 local deque로 들어간다
        for (const ActorRef& target : m->targets)
            target.Send(std::make_unique<IntMessage>(1));

        if (m->fannedOut != nullptr)
            m->fannedOut->store(true);

        while (m->release != nullptr && !m->release->load())
            std::this_thread::yield();
    }
};

// 한 건 처리에 시간이 걸린다. owner 혼자 다 처리해버리기 전에 thief가 끼어들
// 여지를 만든다.
class SlowRecordingActor : public Actor {
public:
    explicit SlowRecordingActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        {
            std::lock_guard<std::mutex> guard(_stats->threadIdLock);
            _stats->threadIds.insert(std::this_thread::get_id());
        }
        _stats->handled.fetch_add(1);
    }

private:
    StatsPtr _stats;
};

// 처리 시점에 "상대가 얼마나 진행했는지"를 sum에 기록한다.
// injection이 굶었는지 판정하는 데 쓴다.
class ObserverActor : public Actor {
public:
    ObserverActor(StatsPtr self, StatsPtr other)
        : _self(std::move(self)), _other(std::move(other)) {
    }

    void Handle(Message&) override {
        _self->sum.store(_other->handled.load());
        _self->handled.fetch_add(1);
    }

private:
    StatsPtr _self;
    StatsPtr _other;
};

}   // namespace TestActors

// ---------------------------------------------------------------------------

inline void PrintStats() {
    const MyUtils::Runtime::Stats s = MyUtils::Runtime::SnapshotStats();

    std::printf("\n--- runtime stats ---\n");
    std::printf("  workers        : %zu\n", MyUtils::Runtime::WorkerCount());
    std::printf("  send           : accepted %llu / rejected %llu\n",
        (unsigned long long)s.sendAccepted, (unsigned long long)s.sendRejected);
    std::printf("  schedule       : %llu (cancelled by shutdown %llu)\n",
        (unsigned long long)s.scheduleCount,
        (unsigned long long)s.scheduleAbortedSystemStopping);
    std::printf("  batch          : %llu runs, %llu messages\n",
        (unsigned long long)s.runCount, (unsigned long long)s.messagesHandled);
    if (s.runCount > 0) {
        std::printf("  avg per batch  : %.2f (budget %zu)\n",
            double(s.messagesHandled) / double(s.runCount),
            MyUtils::Actors::DEFAULT_MESSAGE_BUDGET);
        std::printf("  avg queue wait : %.1f us\n",
            double(s.queueWaitUsTotal) / double(s.runCount));
    }
    std::printf("  actor          : spawn %llu / finalize %llu / handler exceptions %llu\n",
        (unsigned long long)s.spawnCount, (unsigned long long)s.finalizeCount,
        (unsigned long long)s.handlerExceptionCount);
    std::printf("  worker sleep   : %llu\n", (unsigned long long)s.workerSleepCount);

    // --- 스케줄러 라우팅 ---
    const unsigned long long pushTotal = s.localPushCount + s.injectionPushCount;
    if (pushTotal > 0) {
        std::printf("  task route     : local %llu / injection %llu (local %.1f%%)\n",
            (unsigned long long)s.localPushCount,
            (unsigned long long)s.injectionPushCount,
            100.0 * double(s.localPushCount) / double(pushTotal));
    }
}

inline int ReportResult() {
    PrintStats();
    std::printf("\n%s  (%d checks, %d failures)\n",
        GFailCount == 0 ? "ALL PASS" : "FAILED", GCheckCount, GFailCount);
    return GFailCount == 0 ? 0 : 1;
}
