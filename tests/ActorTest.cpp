// Actor Runtime correctness test
//
// design/actor-runtime.md §10 의 invariant <-> test 표를 그대로 구현한다.
// 테스트 이름은 그 표와 일치해야 한다. 이름을 바꾸면 스펙도 같이 고칠 것.

#include "MyUtils/Actor.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <vector>

using namespace MyUtils::Actors;

// Spawn이 accepting fast path를 통과한 뒤 ACB를 할당하는 지점에서 멈추기 위한
// test-only gate. 이 실행 순서를 강제해야 Shutdown과 Registry 등록의 선형화를
// flaky한 stress loop 없이 결정적으로 검증할 수 있다.
static thread_local bool GPauseNextAllocation = false;
static std::atomic<bool> GAllocationPaused{ false };
static std::atomic<bool> GReleaseAllocation{ false };

void* operator new(std::size_t size) {
    if (GPauseNextAllocation) {
        GPauseNextAllocation = false;
        GAllocationPaused.store(true);
        while (!GReleaseAllocation.load())
            std::this_thread::yield();
    }

    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

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

// ---------------------------------------------------------------------------
// 스케줄러 라우팅 테스트용
// ---------------------------------------------------------------------------

// handler 안에서 여러 액터에게 뿌린다. worker 스레드에서 Send하므로 만들어지는
// runnable이 전부 그 worker의 local deque로 들어간다.
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

// 다른 ActorSystem의 액터에게 전달한다
struct ForwardMessage : Message {
    explicit ForwardMessage(ActorRef t) : target(std::move(t)) {}
    ActorRef target;
};

class ForwardingActor : public Actor {
public:
    explicit ForwardingActor(StatsPtr stats) : _stats(std::move(stats)) {}

    void Handle(Message& message) override {
        _stats->handled.fetch_add(1);
        if (auto* m = dynamic_cast<ForwardMessage*>(&message))
            m->target.Send(std::make_unique<IntMessage>(7));
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

static void TestSpawnConcurrentWithShutdownRejected() {
    Section("SpawnConcurrentWithShutdownRejected");

    ActorSystem system(1);
    auto stats = std::make_shared<Stats>();
    ActorRef spawned;

    GAllocationPaused.store(false);
    GReleaseAllocation.store(false);

    std::thread spawner([&] {
        // Actor 생성은 미리 끝낸다. 다음 할당은 Spawn이 accepting을 확인한 뒤
        // 수행하는 ACB 할당이므로 그 정확한 경합 창에서 멈춘다.
        auto actor = std::make_unique<CountingActor>(stats);
        GPauseNextAllocation = true;
        spawned = system.Spawn(std::move(actor));
    });

    const bool paused = WaitUntil([] { return GAllocationPaused.load(); });
    CHECK(paused);
    if (paused)
        system.Shutdown();

    GReleaseAllocation.store(true);
    spawner.join();

    // Shutdown의 등록 gate가 먼저 닫혔으므로 Spawn은 실패해야 하고,
    // snapshot 이후 Registry에 Actor가 남아서는 안 된다.
    CHECK(!spawned.IsValid());
    CHECK(system.LiveActorCount() == 0);
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

    // handler 안에서 자기 자신에게 3건을 보낸 뒤 Stop한다. acceptance gate가 같은
    // mailbox 락을 쓰므로, 락을 쥔 채 Handle을 불렀다면 여기서 데드락한다.
    CHECK(actor.Send(std::make_unique<StopSelfMessage>(actor, 3)));

    CHECK(WaitUntil([&] { return system.LiveActorCount() == 0; }));

    // Stop 전에 보냈으므로 3건 모두 accept되었다
    CHECK(stats->sum.load() == 3);

    // 그런데 하나도 실행되지 않아야 한다. consumption boundary가 없으면 worker가
    // drain 루프 안에 그대로 남아 budget 한도까지 계속 처리한다 (여기서는 4가 된다).
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
// 스케줄러 — work stealing, injection, 라우팅 격리
// ---------------------------------------------------------------------------

static void TestWorkStealingBalancesLoad() {
    Section("WorkStealingBalancesLoad");

    constexpr int TARGETS = 48;

    ActorSystem system(4);
    auto stats = std::make_shared<Stats>();

    std::vector<ActorRef> targets;
    targets.reserve(TARGETS);
    for (int i = 0; i < TARGETS; ++i)
        targets.push_back(system.Spawn(std::make_unique<SlowRecordingActor>(stats)));

    const ActorStats before = SnapshotStats();

    // fan-out handler가 worker 위에서 돌면서 48건을 만든다. 전부 그 worker
    // 하나의 local deque로 들어가므로, 나머지가 일하려면 훔쳐가야 한다.
    ActorRef fanOut = system.Spawn(std::make_unique<FanOutActor>());
    CHECK(fanOut.Send(std::make_unique<FanOutMessage>(targets)));

    CHECK(WaitUntil([&] { return stats->handled.load() == TARGETS; }, 10000));

    const ActorStats after = SnapshotStats();

    // local deque로 라우팅되었는가
    CHECK(after.localPushCount > before.localPushCount);

    // 한 worker가 다 처리하지 않았다 = 나머지가 훔쳐갔다는 뜻이다.
    // 48건이 전부 한 worker의 local deque로 들어갔으므로, 다른 worker가
    // 처리한 것이 있다면 steal 말고는 경로가 없다.
    std::size_t threadCount = 0;
    {
        std::lock_guard<std::mutex> guard(stats->threadIdLock);
        threadCount = stats->threadIds.size();
    }
    CHECK(threadCount >= 2);
    std::printf("  처리 worker 수 : %zu\n", threadCount);
}

static void TestInjectionFromExternalThread() {
    Section("InjectionFromExternalThread");

    ActorSystem system(2);
    auto stats = std::make_shared<Stats>();
    ActorRef actor = system.Spawn(std::make_unique<CountingActor>(stats));

    const ActorStats before = SnapshotStats();

    // worker가 아닌 스레드다. TLS의 runtime이 nullptr이라 injection으로 가야 한다.
    std::thread producer([&] {
        for (int i = 0; i < 100; ++i)
            actor.Send(std::make_unique<IntMessage>(1));
    });
    producer.join();

    CHECK(WaitUntil([&] { return stats->handled.load() == 100; }));

    const ActorStats after = SnapshotStats();
    CHECK(after.injectionPushCount > before.injectionPushCount);
}

static void TestInjectionNotStarvedByLocalWork() {
    Section("InjectionNotStarvedByLocalWork");

    constexpr int PILE = 300;

    // worker 1개가 핵심이다. 그 하나의 local deque에 일이 쌓여 있는 동안
    // injection을 주기적으로 보지 않으면 영영 확인하지 않는다.
    //
    // 타이밍에 기대지 않는다. fan-out handler가 local deque를 채운 뒤 그대로
    // 붙잡고 있으므로, 그 사이에 injection을 채우고 나서 놓아준다.
    ActorSystem system(1);

    auto pileStats = std::make_shared<Stats>();
    auto observerStats = std::make_shared<Stats>();

    std::vector<ActorRef> pile;
    pile.reserve(PILE);
    for (int i = 0; i < PILE; ++i)
        pile.push_back(system.Spawn(std::make_unique<CountingActor>(pileStats)));

    ActorRef observer =
        system.Spawn(std::make_unique<ObserverActor>(observerStats, pileStats));

    std::atomic<bool> fannedOut{ false };
    std::atomic<bool> release{ false };

    auto fanOutMessage = std::make_unique<FanOutMessage>(pile);
    fanOutMessage->fannedOut = &fannedOut;
    fanOutMessage->release = &release;

    ActorRef fanOut = system.Spawn(std::make_unique<FanOutActor>());
    CHECK(fanOut.Send(std::move(fanOutMessage)));

    // local deque에 PILE건이 쌓였고 worker는 아직 handler 안에 붙잡혀 있다
    CHECK(WaitUntil([&] { return fannedOut.load(); }));

    // 이제 injection에 넣는다. main은 worker가 아니므로 반드시 injection이다.
    CHECK(observer.Send(std::make_unique<IntMessage>(1)));
    release.store(true);

    CHECK(WaitUntil([&] { return observerStats->handled.load() == 1; }, 10000));

    // observer가 처리된 시점에 local pile이 얼마나 소진돼 있었나.
    // pile의 각 액터는 한 번 실행에 1건만 처리하므로 tick 1회 = 1건이고,
    // 주기가 61이므로 최대 61건 안에 잡혀야 한다.
    // 주기 확인이 없으면 PILE을 전부 비운 뒤에야(= PILE) 처리된다.
    const int pileProgress = observerStats->sum.load();
    CHECK(pileProgress < PILE / 2);
    std::printf("  local %d건 중 %d건 시점에 injection 처리됨 (주기 %zu)\n",
        PILE, pileProgress, INJECTION_POLL_INTERVAL);

    CHECK(WaitUntil([&] { return pileStats->handled.load() == PILE; }, 10000));
}

static void TestCrossRuntimeIsolation() {
    Section("CrossRuntimeIsolation");

    // 시스템 A의 worker가 시스템 B의 액터에게 Send한다.
    // TLS가 index만 담고 있으면 B의 runnable이 A의 local deque로 샌다.
    ActorSystem systemA(2);
    ActorSystem systemB(2);

    auto statsA = std::make_shared<Stats>();
    auto statsB = std::make_shared<Stats>();

    ActorRef actorB = systemB.Spawn(std::make_unique<CountingActor>(statsB));
    ActorRef actorA = systemA.Spawn(std::make_unique<ForwardingActor>(statsA));

    const ActorStats before = SnapshotStats();

    CHECK(actorA.Send(std::make_unique<ForwardMessage>(actorB)));

    CHECK(WaitUntil([&] { return statsB->handled.load() == 1; }));
    CHECK(statsB->sum.load() == 7);

    const ActorStats after = SnapshotStats();

    // 이 구간의 push는 둘 다 injection이어야 한다.
    //   main -> A의 액터        : main은 worker가 아니다
    //   A의 worker -> B의 액터  : A의 worker는 B의 worker가 아니다
    // runtime 비교가 빠지면 두 번째가 local push로 잡힌다.
    CHECK(after.localPushCount == before.localPushCount);
    CHECK(after.injectionPushCount >= before.injectionPushCount + 2);

    systemA.Shutdown();
    systemB.Shutdown();
}

// ---------------------------------------------------------------------------

static void PrintStats() {
    const ActorStats s = SnapshotStats();

    std::printf("\n--- actor stats ---\n");
    std::printf("  send           : accepted %llu / rejected %llu\n",
        (unsigned long long)s.sendAccepted, (unsigned long long)s.sendRejected);
    std::printf("  schedule       : %llu (시스템 종료로 취소 %llu)\n",
        (unsigned long long)s.scheduleCount,
        (unsigned long long)s.scheduleAbortedSystemStopping);
    std::printf("  batch          : %llu회, 메시지 %llu건\n",
        (unsigned long long)s.runCount, (unsigned long long)s.messagesHandled);
    if (s.runCount > 0) {
        std::printf("  배치당 평균    : %.2f건 (budget %zu)\n",
            double(s.messagesHandled) / double(s.runCount),
            DEFAULT_MESSAGE_BUDGET);
        std::printf("  큐 대기 평균   : %.1f us\n",
            double(s.queueWaitUsTotal) / double(s.runCount));
    }
    std::printf("  actor          : spawn %llu / finalize %llu / handler 예외 %llu\n",
        (unsigned long long)s.spawnCount, (unsigned long long)s.finalizeCount,
        (unsigned long long)s.handlerExceptionCount);
    std::printf("  worker sleep   : %llu회\n", (unsigned long long)s.workerSleepCount);

    // --- 스케줄러 라우팅 ---
    const unsigned long long pushTotal = s.localPushCount + s.injectionPushCount;
    if (pushTotal > 0) {
        std::printf("  runnable 라우팅: local %llu / injection %llu (local %.1f%%)\n",
            (unsigned long long)s.localPushCount,
            (unsigned long long)s.injectionPushCount,
            100.0 * double(s.localPushCount) / double(pushTotal));
    }
}

int main() {
    TestConcurrentExecutionForbidden();
    TestMultiProducerMailbox();
    TestMessageNotLostDuringIdleTransition();
    TestDelayedRunnableAfterStop();
    TestActorMigratesBetweenWorkers();
    TestSendAfterStopRejected();
    TestStopSendRace();
    TestSendAfterShutdownRejected();
    TestSpawnConcurrentWithShutdownRejected();
    TestShutdownWithPendingWork();
    TestSelfSendFromHandler();
    TestSelfStopFromHandler();
    TestSpawnFromHandler();
    TestHandlerExceptionStopsActor();
    TestWorkStealingBalancesLoad();
    TestInjectionFromExternalThread();
    TestInjectionNotStarvedByLocalWork();
    TestCrossRuntimeIsolation();

    PrintStats();

    std::printf("\n%s  (%d checks, %d failures)\n",
        GFailCount == 0 ? "ALL PASS" : "FAILED", GCheckCount, GFailCount);
    return GFailCount == 0 ? 0 : 1;
}
