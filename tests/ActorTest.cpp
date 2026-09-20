// Actor Runtime correctness test — worker 4개
//
// design/actor-runtime.md §10 의 invariant <-> test 표를 그대로 구현한다.
// 테스트 이름은 그 표와 일치해야 한다. 이름을 바꾸면 스펙도 같이 고칠 것.
//
// 여기에는 **런타임을 종료하지 않는** 테스트만 둔다. 종료는 프로세스당 한 번뿐이라
// 종료를 검증하는 것들은 ActorShutdownTest로 갔다.

#include "ActorTestSupport.h"

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

using namespace MyUtils::Actors;
using namespace TestActors;
namespace Rt = MyUtils::Runtime;

static constexpr std::size_t WORKERS = 4;

// ---------------------------------------------------------------------------

static void TestWorkerCountConfiguration() {
    Section("WorkerCountConfiguration");

    // [N-2] WorkerCount()는 런타임을 기동시키지 않는다. 기동시켰다면 바로 아래의
    // SetWorkerCount가 계약 위반으로 프로세스를 죽이므로, 여기를 지나온다는 것
    // 자체가 증거다.
    const std::size_t beforeConfig = Rt::WorkerCount();
    CHECK(beforeConfig >= 1);

    Rt::SetWorkerCount(WORKERS);

    // [N-3] 기동 전 설정이 반영된다. last-write-wins.
    Rt::SetWorkerCount(WORKERS);
    CHECK(Rt::WorkerCount() == WORKERS);
}

static void TestConcurrentExecutionForbidden() {
    Section("ConcurrentExecutionForbidden");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));
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

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

static void TestMultiProducerMailbox() {
    Section("MultiProducerMailbox");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));

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

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

static void TestMessageNotLostDuringIdleTransition() {
    Section("MessageNotLostDuringIdleTransition");

    const std::size_t baseline = LiveActorCount();

    // drain 직후 IDLE 전이와 enqueue가 겹치도록 쉬지 않고 밀어 넣는다.
    // 이 패턴이 프로토콜 A의 경합 창을 최대로 만든다.
    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));

    constexpr int ROUNDS = 2000;
    for (int i = 0; i < ROUNDS; ++i) {
        CHECK(actor.Send(std::make_unique<IntMessage>(1)));
        // 대기하지 않고 바로 다음을 보내 RUNNING/IDLE 경계에 걸리게 한다
        if ((i % 8) == 0)
            std::this_thread::yield();
    }

    CHECK(WaitUntil([&] { return stats->handled.load() == ROUNDS; }));
    CHECK(stats->handled.load() == ROUNDS);

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

static void TestDelayedRunnableAfterStop() {
    Section("DelayedRunnableAfterStop");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));

    for (int i = 0; i < 500; ++i)
        actor.Send(std::make_unique<IntMessage>(1));

    actor.Stop();

    // Stop 이후의 Send는 전부 거부된다
    for (int i = 0; i < 100; ++i)
        CHECK(!actor.Send(std::make_unique<IntMessage>(1)));

    // 이미 큐에 있던 task가 뒤늦게 실행돼도 UAF가 없어야 한다
    CHECK(WaitForLiveActors(baseline));
}

static void TestActorMigratesBetweenWorkers() {
    Section("ActorMigratesBetweenWorkers");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<ThreadRecordingActor>(stats));

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
    std::printf("  workers used   : %zu\n", distinct);
    // 통계적 성격의 유일한 테스트다. 400회 재스케줄에서 한 worker만 잡을 확률은 낮다.
    CHECK(distinct >= 2);

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

static void TestSendAfterStopRejected() {
    Section("SendAfterStopRejected");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));

    CHECK(actor.Send(std::make_unique<IntMessage>(1)));
    actor.Stop();

    // Stop boundary 이후에는 반드시 실패한다 (선형화 보장)
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));

    // 중복 Stop도 안전하다
    actor.Stop();
    actor.Stop();
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));

    CHECK(WaitForLiveActors(baseline));
}

static void TestStopSendRace() {
    Section("StopSendRace");

    const std::size_t baseline = LiveActorCount();

    constexpr int ITERATIONS = 200;
    int totalAccepted = 0;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        auto stats = std::make_shared<Stats>();
        ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));

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

    std::printf("  %d iterations, %d accepted in total\n", ITERATIONS, totalAccepted);

    // 200개 액터가 전부 Stop을 통해 정리되었다
    CHECK(WaitForLiveActors(baseline));
}

static void TestSelfSendFromHandler() {
    Section("SelfSendFromHandler");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<ChainActor>(stats));

    constexpr int DEPTH = 500;
    CHECK(actor.Send(std::make_unique<ChainMessage>(actor, DEPTH)));

    // 락을 쥔 채 Handle을 불렀다면 여기서 타임아웃한다
    CHECK(WaitUntil([&] { return stats->handled.load() == DEPTH + 1; }));
    CHECK(stats->handled.load() == DEPTH + 1);

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

static void TestSelfStopFromHandler() {
    Section("SelfStopFromHandler");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<SelfStoppingActor>(stats));

    // handler 안에서 자기 자신에게 3건을 보낸 뒤 Stop한다. acceptance gate가 같은
    // mailbox 락을 쓰므로, 락을 쥔 채 Handle을 불렀다면 여기서 데드락한다.
    CHECK(actor.Send(std::make_unique<StopSelfMessage>(actor, 3)));

    CHECK(WaitForLiveActors(baseline));

    // Stop 전에 보냈으므로 3건 모두 accept되었다
    CHECK(stats->sum.load() == 3);

    // 그런데 하나도 실행되지 않아야 한다. consumption boundary가 없으면 worker가
    // drain 루프 안에 그대로 남아 budget 한도까지 계속 처리한다 (여기서는 4가 된다).
    CHECK(stats->handled.load() == 1);

    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
}

static void TestSpawnFromHandler() {
    Section("SpawnFromHandler");

    const std::size_t baseline = LiveActorCount();

    auto parentStats = std::make_shared<Stats>();
    auto childStats = std::make_shared<Stats>();

    auto child = std::make_shared<ActorRef>();

    ActorRef parent = Spawn(std::make_unique<SpawningActor>(parentStats));
    CHECK(parent.Send(std::make_unique<SpawnMessage>(childStats, child)));

    CHECK(WaitUntil([&] { return parentStats->handled.load() == 1; }));
    CHECK(WaitUntil([&] { return childStats->handled.load() == 1; }));
    CHECK(childStats->sum.load() == 7);

    // handler 안에서 만든 자식이 유효했다
    CHECK(parentStats->sum.load() == 1);
    CHECK(child->IsValid());

    // 자식은 parent가 Stop해도 살아 있다. Registry가 수명을 쥐기 때문이다.
    parent.Stop();
    CHECK(WaitForLiveActors(baseline + 1));

    child->Stop();
    CHECK(WaitForLiveActors(baseline));
}

static void TestHandlerExceptionStopsActor() {
    Section("HandlerExceptionStopsActor");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<ThrowingActor>(stats));

    // 5건을 한 번에 넣는다. 첫 건에서 던지면 나머지는 처리되지 않아야 한다.
    for (int i = 0; i < 5; ++i)
        CHECK(actor.Send(std::make_unique<IntMessage>(1)));

    CHECK(WaitForLiveActors(baseline));

    // drain 루프를 즉시 탈출했으므로 딱 한 건만 시도된다
    CHECK(stats->handled.load() == 1);
    // 격리되었으므로 이후 Send는 거부된다
    CHECK(!actor.Send(std::make_unique<IntMessage>(1)));
}

// ---------------------------------------------------------------------------
// 스케줄러 — work stealing, injection
// ---------------------------------------------------------------------------

static void TestWorkStealingBalancesLoad() {
    Section("WorkStealingBalancesLoad");

    constexpr int TARGETS = 48;

    const std::size_t baseline = LiveActorCount();
    auto stats = std::make_shared<Stats>();

    std::vector<ActorRef> targets;
    targets.reserve(TARGETS);
    for (int i = 0; i < TARGETS; ++i)
        targets.push_back(Spawn(std::make_unique<SlowRecordingActor>(stats)));

    const Rt::Stats before = Rt::SnapshotStats();

    // fan-out handler가 worker 위에서 돌면서 48건을 만든다. 전부 그 worker
    // 하나의 local deque로 들어가므로, 나머지가 일하려면 훔쳐가야 한다.
    ActorRef fanOut = Spawn(std::make_unique<FanOutActor>());
    CHECK(fanOut.Send(std::make_unique<FanOutMessage>(targets)));

    CHECK(WaitUntil([&] { return stats->handled.load() == TARGETS; }, 10000));

    const Rt::Stats after = Rt::SnapshotStats();

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
    std::printf("  workers that processed: %zu\n", threadCount);

    fanOut.Stop();
    for (ActorRef& target : targets)
        target.Stop();
    CHECK(WaitForLiveActors(baseline));
}

static void TestInjectionFromExternalThread() {
    Section("InjectionFromExternalThread");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));

    const Rt::Stats before = Rt::SnapshotStats();

    // worker가 아닌 스레드다. TLS의 isWorker가 false라 injection으로 가야 한다.
    std::thread producer([&] {
        for (int i = 0; i < 100; ++i)
            actor.Send(std::make_unique<IntMessage>(1));
    });
    producer.join();

    CHECK(WaitUntil([&] { return stats->handled.load() == 100; }));

    const Rt::Stats after = Rt::SnapshotStats();
    CHECK(after.injectionPushCount > before.injectionPushCount);

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

// ---------------------------------------------------------------------------

int main() {
    TestWorkerCountConfiguration();

    TestConcurrentExecutionForbidden();
    TestMultiProducerMailbox();
    TestMessageNotLostDuringIdleTransition();
    TestDelayedRunnableAfterStop();
    TestActorMigratesBetweenWorkers();
    TestSendAfterStopRejected();
    TestStopSendRace();
    TestSelfSendFromHandler();
    TestSelfStopFromHandler();
    TestSpawnFromHandler();
    TestHandlerExceptionStopsActor();
    TestWorkStealingBalancesLoad();
    TestInjectionFromExternalThread();

    return ReportResult();
}
