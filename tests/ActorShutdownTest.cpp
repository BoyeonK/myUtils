// Actor Runtime shutdown test — worker 2개
//
// 런타임 종료는 프로세스당 한 번뿐이므로, 종료를 검증하는 invariant를 전부
// **하나의 종료 시나리오**로 묶는다.
//
//   - 남은 메시지/task가 있어도 shutdown이 안전하다
//   - Shutdown gate 이후 Registry에 새 Actor가 등록되지 않는다
//   - Shutdown 후 stale ActorRef의 Send는 실패한다
//   - 종료 후 Spawn은 무효 핸들을 돌려준다
//   - 종료 요청은 여러 번 불러도 안전하다
//
// design/actor-runtime.md §8·§10 을 따른다.

#include "ActorTestSupport.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>
#include <vector>

using namespace MyUtils::Actors;
using namespace TestActors;
namespace Rt = MyUtils::Runtime;

static constexpr std::size_t WORKERS = 2;

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

// ---------------------------------------------------------------------------

static void TestShutdownScenario() {
    Section("ShutdownScenario");

    // Actor마다 별도 Stats를 준다. 하나를 공유하면 inFlight 카운터도 공유되어,
    // 서로 다른 Actor가 병렬 실행되는 정상 동작이 위반으로 잡힌다.
    constexpr int ACTORS = 16;
    constexpr int ROUNDS = 200;

    // [필수] handler에 지연을 준다. 지연이 없으면 worker 2개가 발송 속도를
    // 따라잡아 종료 시점에 남은 일이 없고, 그러면 discard 경로를 전혀 밟지
    // 않은 채 통과한다.
    constexpr int HANDLE_DELAY_MS = 2;

    std::vector<StatsPtr> allStats;
    std::vector<ActorRef> actors;
    for (int i = 0; i < ACTORS; ++i) {
        auto stats = std::make_shared<Stats>();
        allStats.push_back(stats);
        actors.push_back(Spawn(std::make_unique<CountingActor>(stats, HANDLE_DELAY_MS)));
        CHECK(actors.back().IsValid());
    }

    const ActorRef stale = actors.front();
    ActorRef spawnedDuringShutdown;

    GAllocationPaused.store(false);
    GReleaseAllocation.store(false);

    std::thread spawner([&] {
        // Actor 생성은 미리 끝낸다. 다음 할당은 Spawn이 accepting을 확인한 뒤
        // 수행하는 ACB 할당이므로 그 정확한 경합 창에서 멈춘다.
        auto stats = std::make_shared<Stats>();
        auto actor = std::make_unique<CountingActor>(stats);
        GPauseNextAllocation = true;
        spawnedDuringShutdown = Spawn(std::move(actor));
    });

    const bool paused = WaitUntil([] { return GAllocationPaused.load(); });
    CHECK(paused);

    // [순서 주의] 밀어 넣는 것은 **일시정지를 관측한 뒤**여야 한다. 먼저 보내면
    // 할당 gate를 기다리는 동안 worker가 전부 비워버려 "pending이 남은 상태의
    // 종료"를 검증하지 못한다(실제로 그렇게 됐던 적이 있다).
    //
    // 할당 gate는 spawner 스레드의 thread_local이라 여기 main의 할당에는
    // 걸리지 않는다.
    for (int round = 0; round < ROUNDS; ++round) {
        for (ActorRef& actor : actors)
            actor.Send(std::make_unique<IntMessage>(1));
    }

    // --- 여기서 몸체 전체가 종료된다. 프로세스당 한 번뿐이다 ---
    if (paused)
        Rt::Shutdown();

    // [종료 반환 시점에 실행 중인 handler가 없다]
    // 돌고 있는 handler가 있었다면 그 Actor의 inFlight가 1이다. 반환 직후에
    // 읽어야 의미가 있으므로 다른 어떤 검사보다 먼저 본다.
    int inFlightTotal = 0;
    int handledRightAfterShutdown = 0;
    for (const StatsPtr& stats : allStats) {
        inFlightTotal += stats->inFlight.load();
        handledRightAfterShutdown += stats->handled.load();
    }
    CHECK(inFlightTotal == 0);

    GReleaseAllocation.store(true);
    spawner.join();

    // 그리고 더 이상 늘지 않는다. worker가 살아 있다면 3,000건 넘게 남은 일을
    // 계속 처리했을 것이다.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int handledLater = 0;
    for (const StatsPtr& stats : allStats)
        handledLater += stats->handled.load();
    CHECK(handledLater == handledRightAfterShutdown);

    // [SpawnConcurrentWithShutdownRejected]
    // Shutdown의 등록 gate가 먼저 닫혔으므로 Spawn은 실패해야 한다.
    CHECK(!spawnedDuringShutdown.IsValid());

    // [ShutdownWithPendingWork]
    // pending이 남아 있어도 Registry가 전부 정리되었다.
    CHECK(LiveActorCount() == 0);

    // [SendAfterShutdownRejected]
    CHECK(!stale.Send(std::make_unique<IntMessage>(1)));
    for (ActorRef& actor : actors)
        CHECK(!actor.Send(std::make_unique<IntMessage>(1)));

    // stale 핸들의 Stop도 안전하다. 이미 DEAD라 그 자리에서 돌아선다.
    stale.Stop();
    stale.Stop();

    // [N-4] 종료 후 Spawn은 무효 핸들을 돌려준다.
    auto lateStats = std::make_shared<Stats>();
    const ActorRef late = Spawn(std::make_unique<CountingActor>(lateStats));
    CHECK(!late.IsValid());
    CHECK(!late.Send(std::make_unique<IntMessage>(1)));

    // [N-5] 종료 요청은 멱등이다.
    Rt::Shutdown();
    Rt::Shutdown();
    CHECK(LiveActorCount() == 0);

    // worker join이 끝난 뒤이므로 카운터를 동기화 없이 읽어도 확정값이다.
    int handledTotal = 0;
    for (const StatsPtr& stats : allStats) {
        handledTotal += stats->handled.load();
        // Actor 단위 직렬화는 shutdown 중에도 유지되어야 한다
        CHECK(stats->concurrentViolations.load() == 0);
    }
    std::printf("  messages handled before stop: %d of %d\n",
        handledTotal, ACTORS * ROUNDS);

    // accept된 것보다 많이 처리될 수는 없다. 남은 것은 종료가 버린다 —
    // "true = accepted, not delivered" 계약 그대로다.
    CHECK(handledTotal <= ACTORS * ROUNDS);
    if (handledTotal == ACTORS * ROUNDS) {
        std::printf("  [warn] pending work was fully drained before shutdown;"
            " this run did not exercise the discard path\n");
    }

    // [주의] LT1("Send의 DEAD 검사가 몸체 접근보다 앞선다")은 직접 관측하지
    // 않는다. 몸체는 프로세스 전역 static이라 이 프로세스 안에서 파괴시킬 수
    // 없기 때문이다. 위의 stale Send/Stop이 크래시 없이 false를 돌려주는 것으로
    // 간접 확인한다 — L1을 데드락 세 패턴으로 잡는 것과 같은 방식이다.
}

// ---------------------------------------------------------------------------

int main() {
    Rt::SetWorkerCount(WORKERS);
    CHECK(Rt::WorkerCount() == WORKERS);

    TestShutdownScenario();

    return ReportResult();
}
