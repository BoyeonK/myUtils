// Actor Runtime correctness test — worker 1개
//
// worker가 하나여야 결정적인 것만 둔다. worker가 여럿이면 다른 worker가 훔쳐가
// "local deque가 차 있는 동안 injection을 보지 않는다"는 상황 자체가 만들어지지
// 않고, 나머지를 handler로 붙잡아 재현하려 해도 어느 worker에 배정될지 보장되지
// 않아 flaky해진다.
//
// design/actor-runtime.md §10 의 invariant <-> test 표를 따른다.

#include "ActorTestSupport.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

using namespace MyUtils::Actors;
using namespace TestActors;
namespace Rt = MyUtils::Runtime;

static constexpr std::size_t WORKERS = 1;

// ---------------------------------------------------------------------------

static void TestInjectionNotStarvedByLocalWork() {
    Section("InjectionNotStarvedByLocalWork");

    constexpr int PILE = 300;

    const std::size_t baseline = LiveActorCount();

    // worker 1개가 핵심이다. 그 하나의 local deque에 일이 쌓여 있는 동안
    // injection을 주기적으로 보지 않으면 영영 확인하지 않는다.
    //
    // 타이밍에 기대지 않는다. fan-out handler가 local deque를 채운 뒤 그대로
    // 붙잡고 있으므로, 그 사이에 injection을 채우고 나서 놓아준다.
    auto pileStats = std::make_shared<Stats>();
    auto observerStats = std::make_shared<Stats>();

    std::vector<ActorRef> pile;
    pile.reserve(PILE);
    for (int i = 0; i < PILE; ++i)
        pile.push_back(Spawn(std::make_unique<CountingActor>(pileStats)));

    ActorRef observer = Spawn(std::make_unique<ObserverActor>(observerStats, pileStats));

    std::atomic<bool> fannedOut{ false };
    std::atomic<bool> release{ false };

    auto fanOutMessage = std::make_unique<FanOutMessage>(pile);
    fanOutMessage->fannedOut = &fannedOut;
    fanOutMessage->release = &release;

    ActorRef fanOut = Spawn(std::make_unique<FanOutActor>());
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
    std::printf("  injection handled after %d of %d local items (interval %zu)\n",
        pileProgress, PILE, Rt::INJECTION_POLL_INTERVAL);

    CHECK(WaitUntil([&] { return pileStats->handled.load() == PILE; }, 10000));

    fanOut.Stop();
    observer.Stop();
    for (ActorRef& actor : pile)
        actor.Stop();
    CHECK(WaitForLiveActors(baseline, 10000));
}

// worker가 하나뿐이어도 외부 스레드의 Send가 처리된다. work stealing이 없는
// 구성에서 injection 경로만으로 진행이 보장되는지 본다.
static void TestSingleWorkerMakesProgress() {
    Section("SingleWorkerMakesProgress");

    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));

    constexpr int ROUNDS = 500;
    for (int i = 0; i < ROUNDS; ++i)
        CHECK(actor.Send(std::make_unique<IntMessage>(1)));

    CHECK(WaitUntil([&] { return stats->handled.load() == ROUNDS; }));
    CHECK(stats->sum.load() == ROUNDS);
    CHECK(stats->concurrentViolations.load() == 0);

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

// ---------------------------------------------------------------------------

int main() {
    Rt::SetWorkerCount(WORKERS);
    CHECK(Rt::WorkerCount() == WORKERS);

    TestInjectionNotStarvedByLocalWork();
    TestSingleWorkerMakesProgress();

    return ReportResult();
}
