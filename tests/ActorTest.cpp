// Actor Runtime correctness test
//
// design/actor-runtime.md §10 의 invariant <-> test 표를 그대로 구현한다.
// 테스트 이름은 그 표와 일치해야 한다. 이름을 바꾸면 스펙도 같이 고칠 것.

#include "MyUtils/Actor.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace MyUtils::Actors;

static int GFailCount = 0;
static int GCheckCount = 0;

#define CHECK(expr)                                                        \
    do {                                                                   \
        ++GCheckCount;                                                     \
        if (!(expr)) {                                                     \
            std::printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
            ++GFailCount;                                                  \
        }                                                                  \
    } while (0)

static void Section(const char* name) { std::printf("[%s]\n", name); }

template <class Pred>
static bool WaitUntil(Pred pred, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

// ---------------------------------------------------------------------------
// 공용 메시지 / 관측 객체
//
// ACB가 Actor를 unique 소유하고 Finalize에서 파괴하므로(ADR-0010), 테스트는
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

struct IntMessage : Message {
    explicit IntMessage(int v) : value(v) {}
    int value;
};

// ---------------------------------------------------------------------------

namespace {

class CountingActor : public Actor {
public:
    explicit CountingActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message& message) override {
        // 같은 Actor가 동시에 두 worker에서 실행되면 여기서 잡힌다
        if (_stats->inFlight.fetch_add(1) != 0)
            _stats->concurrentViolations.fetch_add(1);

        if (auto* m = dynamic_cast<IntMessage*>(&message))
            _stats->sum.fetch_add(m->value);

        // 겹칠 틈을 넓혀 경합을 드러낸다
        std::this_thread::yield();

        _stats->inFlight.fetch_sub(1);
        _stats->handled.fetch_add(1);
    }

private:
    StatsPtr _stats;
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

// self-stop
struct StopSelfMessage : Message {
    explicit StopSelfMessage(ActorRef s) : self(std::move(s)) {}
    ActorRef self;
};

class SelfStoppingActor : public Actor {
public:
    explicit SelfStoppingActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message& message) override {
        _stats->handled.fetch_add(1);
        if (auto* m = dynamic_cast<StopSelfMessage*>(&message))
            m->self.Stop();   // [L1 검증] 같은 mailbox 락을 쓰므로 락 보유 시 데드락
    }

private:
    StatsPtr _stats;
};

// handler 안에서 Spawn
struct SpawnMessage : Message {
    SpawnMessage(ActorSystem* sys, StatsPtr childStats)
        : system(sys), stats(std::move(childStats)) {}
    ActorSystem* system;
    StatsPtr stats;
};

class SpawningActor : public Actor {
public:
    explicit SpawningActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message& message) override {
        auto* m = dynamic_cast<SpawnMessage*>(&message);
        if (m == nullptr)
            return;

        _stats->handled.fetch_add(1);

        // [L1 검증] registry 락을 잡는다. mailbox 락과 중첩되면 문제가 된다
        ActorRef child = m->system->Spawn(std::make_unique<CountingActor>(m->stats));
        CHECK(child.IsValid());
        child.Send(std::make_unique<IntMessage>(7));
    }

private:
    StatsPtr _stats;
};

}   // namespace

// ---------------------------------------------------------------------------

static void TestConcurrentExecutionForbidden() {
    Section("ConcurrentExecutionForbidden");

    ActorSystem system(4);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<CountingActor>(stats));
    CHECK(actor.IsValid());

    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 500;

    std::vector<std::thread> producers;
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < PER_PRODUCER; ++i)
                actor.Send(std::make_unique<IntMessage>(1));
        });
    }
    for (std::thread& t : producers)
        t.join();

    CHECK(WaitUntil([&] { return stats->handled.load() == PRODUCERS * PER_PRODUCER; }));
    CHECK(stats->concurrentViolations.load() == 0);
    CHECK(stats->handled.load() == PRODUCERS * PER_PRODUCER);
}

static void TestMultiProducerMailbox() {
    Section("MultiProducerMailbox");

    ActorSystem system(4);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<CountingActor>(stats));

    constexpr int PRODUCERS = 6;
    constexpr int PER_PRODUCER = 1000;

    std::vector<std::thread> producers;
    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < PER_PRODUCER; ++i)
                CHECK(actor.Send(std::make_unique<IntMessage>(1)));
        });
    }
    for (std::thread& t : producers)
        t.join();

    const int expected = PRODUCERS * PER_PRODUCER;
    CHECK(WaitUntil([&] { return stats->handled.load() == expected; }));
    // 유실도 중복도 없어야 한다
    CHECK(stats->sum.load() == expected);
}

static void TestMessageNotLostDuringIdleTransition() {
    Section("MessageNotLostDuringIdleTransition");

    // drain 직후 IDLE 전이와 enqueue가 겹치도록, 한 건씩 보내고 처리되기를
    // 기다렸다 다시 보낸다. 이 패턴이 프로토콜 A의 경합 창을 최대로 만든다.
    ActorSystem system(4);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<CountingActor>(stats));

    constexpr int ROUNDS = 2000;
    for (int i = 0; i < ROUNDS; ++i) {
        CHECK(actor.Send(std::make_unique<IntMessage>(1)));
        // 대기하지 않고 바로 다음을 보내 RUNNING/IDLE 경계에 걸리게 한다
        if ((i % 8) == 0)
            std::this_thread::yield();
    }

    CHECK(WaitUntil([&] { return stats->handled.load() == ROUNDS; }));
    CHECK(stats->handled.load() == ROUNDS);
}

static void TestDelayedRunnableAfterStop() {
    Section("DelayedRunnableAfterStop");

    ActorSystem system(4);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<CountingActor>(stats));

    for (int i = 0; i < 500; ++i)
        actor.Send(std::make_unique<IntMessage>(1));

    actor.Stop();

    // Stop 이후의 Send는 전부 거부된다
    for (int i = 0; i < 100; ++i)
        CHECK(!actor.Send(std::make_unique<IntMessage>(1)));

    // 이미 큐에 있던 runnable이 뒤늦게 실행돼도 UAF가 없어야 한다
    CHECK(WaitUntil([&] { return system.LiveActorCount() == 0; }));
    CHECK(system.LiveActorCount() == 0);
}

static void TestActorMigratesBetweenWorkers() {
    Section("ActorMigratesBetweenWorkers");

    ActorSystem system(4);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<ThreadRecordingActor>(stats));

    // 매번 새로 스케줄되도록 한 건 처리를 기다렸다 다음을 보낸다
    constexpr int ROUNDS = 400;
    for (int i = 0; i < ROUNDS; ++i) {
        CHECK(actor.Send(std::make_unique<IntMessage>(1)));
        WaitUntil([&] { return stats->handled.load() > i; }, 1000);
    }

    CHECK(WaitUntil([&] { return stats->handled.load() == ROUNDS; }));

    std::size_t distinct = 0;
    {
        std::lock_guard<std::mutex> guard(stats->threadIdLock);
        distinct = stats->threadIds.size();
    }
    std::printf("  실행된 worker 수: %zu\n", distinct);
    // 통계적 성격의 유일한 테스트다. 400회 재스케줄에서 한 worker만 잡을 확률은 낮다.
    CHECK(distinct >= 2);
}

static void TestSendAfterStopRejected() {
    Section("SendAfterStopRejected");

    ActorSystem system(2);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<CountingActor>(stats));

    CHECK(actor.Send(std::make_unique<IntMessage>(1)));
    actor.Stop();

    // Stop boundary 이후에는 반드시 실패한다 (선형화 보장)
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));

    // 중복 Stop도 안전하다
    actor.Stop();
    actor.Stop();
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
}

static void TestStopSendRace() {
    Section("StopSendRace");

    constexpr int ITERATIONS = 200;
    int totalAccepted = 0;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        ActorSystem system(4);
        auto stats = std::make_shared<Stats>();
        ActorRef actor = system.Spawn(std::make_unique<CountingActor>(stats));

        std::atomic<int> accepted{ 0 };
        std::atomic<bool> go{ false };

        std::vector<std::thread> threads;
        for (int p = 0; p < 3; ++p) {
            threads.emplace_back([&] {
                while (!go.load()) {}
                for (int i = 0; i < 50; ++i) {
                    if (actor.Send(std::make_unique<IntMessage>(1)))
                        accepted.fetch_add(1);
                }
            });
        }
        threads.emplace_back([&] {
            while (!go.load()) {}
            actor.Stop();
        });

        go.store(true);
        for (std::thread& t : threads)
            t.join();

        // Stop이 끝난 뒤에는 어떤 Send도 성공하지 않는다
        CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
        // accept된 것보다 많이 처리될 수는 없다 (discard는 허용)
        CHECK(stats->handled.load() <= accepted.load());
        CHECK(stats->concurrentViolations.load() == 0);

        totalAccepted += accepted.load();
    }

    std::printf("  %d회 반복, 누적 accept %d건\n", ITERATIONS, totalAccepted);
}

static void TestSendAfterShutdownRejected() {
    Section("SendAfterShutdownRejected");

    auto stats = std::make_shared<Stats>();
    ActorRef stale;
    {
        ActorSystem system(2);
        stale = system.Spawn(std::make_unique<CountingActor>(stats));
        CHECK(stale.Send(std::make_unique<IntMessage>(1)));
        system.Shutdown();

        // shutdown 완료 후에는 모든 Actor가 DEAD다
        CHECK(!stale.Send(std::make_unique<IntMessage>(1)));
        CHECK(system.LiveActorCount() == 0);
    }

    // ActorSystem이 사라진 뒤에도 stale 핸들 사용이 안전해야 한다
    CHECK(stale.IsValid());
    CHECK(!stale.Send(std::make_unique<IntMessage>(1)));
    stale.Stop();
}

static void TestShutdownWithPendingWork() {
    Section("ShutdownWithPendingWork");

    // Actor마다 별도 Stats를 준다. 하나를 공유하면 inFlight 카운터도 공유되어,
    // 서로 다른 Actor가 병렬 실행되는 정상 동작이 위반으로 잡힌다.
    std::vector<StatsPtr> allStats;
    {
        ActorSystem system(4);

        std::vector<ActorRef> actors;
        for (int i = 0; i < 16; ++i) {
            auto stats = std::make_shared<Stats>();
            allStats.push_back(stats);
            actors.push_back(system.Spawn(std::make_unique<CountingActor>(stats)));
        }

        for (int round = 0; round < 200; ++round) {
            for (ActorRef& actor : actors)
                actor.Send(std::make_unique<IntMessage>(1));
        }

        // 메시지와 runnable이 잔뜩 남은 상태로 종료한다
        system.Shutdown();
        CHECK(system.LiveActorCount() == 0);

        for (ActorRef& actor : actors)
            CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
    }

    int handledTotal = 0;
    for (const StatsPtr& stats : allStats) {
        handledTotal += stats->handled.load();
        // Actor 단위 직렬화는 shutdown 중에도 유지되어야 한다
        CHECK(stats->concurrentViolations.load() == 0);
    }
    std::printf("  종료 전까지 처리된 메시지: %d건\n", handledTotal);
}

static void TestSelfSendFromHandler() {
    Section("SelfSendFromHandler");

    ActorSystem system(2);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<ChainActor>(stats));

    constexpr int DEPTH = 500;
    CHECK(actor.Send(std::make_unique<ChainMessage>(actor, DEPTH)));

    // 락을 쥔 채 Handle을 불렀다면 여기서 타임아웃한다
    CHECK(WaitUntil([&] { return stats->handled.load() == DEPTH + 1; }));
    CHECK(stats->handled.load() == DEPTH + 1);
}

static void TestSelfStopFromHandler() {
    Section("SelfStopFromHandler");

    ActorSystem system(2);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<SelfStoppingActor>(stats));

    // handler 안에서 자기 자신을 Stop한다. acceptance gate가 같은 mailbox 락을
    // 쓰므로, 락을 쥔 채 Handle을 불렀다면 여기서 데드락한다.
    CHECK(actor.Send(std::make_unique<StopSelfMessage>(actor)));

    CHECK(WaitUntil([&] { return system.LiveActorCount() == 0; }));
    CHECK(stats->handled.load() == 1);
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
}

static void TestSpawnFromHandler() {
    Section("SpawnFromHandler");

    ActorSystem system(2);
    auto parentStats = std::make_shared<Stats>();
    auto childStats = std::make_shared<Stats>();

    ActorRef parent = system.Spawn(std::make_unique<SpawningActor>(parentStats));
    CHECK(parent.Send(std::make_unique<SpawnMessage>(&system, childStats)));

    CHECK(WaitUntil([&] { return parentStats->handled.load() == 1; }));
    CHECK(WaitUntil([&] { return childStats->handled.load() == 1; }));
    CHECK(childStats->sum.load() == 7);
}

static void TestHandlerExceptionStopsActor() {
    Section("HandlerExceptionStopsActor");

    ActorSystem system(2);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<ThrowingActor>(stats));

    // 5건을 한 번에 넣는다. 첫 건에서 던지면 나머지는 처리되지 않아야 한다.
    for (int i = 0; i < 5; ++i)
        CHECK(actor.Send(std::make_unique<IntMessage>(1)));

    CHECK(WaitUntil([&] { return system.LiveActorCount() == 0; }));

    // drain 루프를 즉시 탈출했으므로 딱 한 건만 시도된다
    CHECK(stats->handled.load() == 1);
    // 격리되었으므로 이후 Send는 거부된다
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
}

// ---------------------------------------------------------------------------

int main() {
    TestConcurrentExecutionForbidden();
    TestMultiProducerMailbox();
    TestMessageNotLostDuringIdleTransition();
    TestDelayedRunnableAfterStop();
    TestActorMigratesBetweenWorkers();
    TestSendAfterStopRejected();
    TestStopSendRace();
    TestSendAfterShutdownRejected();
    TestShutdownWithPendingWork();
    TestSelfSendFromHandler();
    TestSelfStopFromHandler();
    TestSpawnFromHandler();
    TestHandlerExceptionStopsActor();

    std::printf("\n%s  (%d checks, %d failures)\n",
        GFailCount == 0 ? "ALL PASS" : "FAILED", GCheckCount, GFailCount);
    return GFailCount == 0 ? 0 : 1;
}
