// Runtime 기본값 경로 테스트
//
// 이 실행 파일은 SetWorkerCount를 **한 번도 부르지 않는다.** 다른 테스트 실행
// 파일은 전부 worker 수를 지정하므로, "설정하지 않아도 동작한다"가 거기서는
// 검증되지 않는다. 그것만을 위한 프로세스다.

#include "ActorTestSupport.h"

#include <cstddef>
#include <memory>
#include <thread>

using namespace MyUtils::Actors;
using namespace TestActors;
namespace Rt = MyUtils::Runtime;

// ---------------------------------------------------------------------------

static void TestDefaultWorkerCount() {
    Section("DefaultWorkerCount");

    // 기동 전에 물어봐도 답이 나온다. 기본값은 hardware_concurrency이며
    // 그것이 0을 보고하면 1로 내린다 — 어느 쪽이든 1 이상이다.
    const std::size_t before = Rt::WorkerCount();
    CHECK(before >= 1);

    // 여러 번 물어도 같은 값이다. 게터가 상태를 바꾸지 않는다.
    CHECK(Rt::WorkerCount() == before);

    const unsigned int hardware = std::thread::hardware_concurrency();
    const std::size_t expected =
        hardware == 0 ? std::size_t{ 1 } : static_cast<std::size_t>(hardware);
    CHECK(before == expected);

    std::printf("  default worker count: %zu (hardware_concurrency %u)\n",
        before, hardware);
}

static void TestRunsWithoutConfiguration() {
    Section("RunsWithoutConfiguration");

    const std::size_t configured = Rt::WorkerCount();

    // 설정 호출 없이 바로 쓴다. 첫 Spawn이 런타임을 기동시킨다.
    const std::size_t baseline = LiveActorCount();

    auto stats = std::make_shared<Stats>();
    ActorRef actor = Spawn(std::make_unique<CountingActor>(stats));
    CHECK(actor.IsValid());

    constexpr int ROUNDS = 1000;
    for (int i = 0; i < ROUNDS; ++i)
        CHECK(actor.Send(std::make_unique<IntMessage>(1)));

    CHECK(WaitUntil([&] { return stats->handled.load() == ROUNDS; }));
    CHECK(stats->sum.load() == ROUNDS);
    CHECK(stats->concurrentViolations.load() == 0);

    // 기동 후에도 보고값이 바뀌지 않는다
    CHECK(Rt::WorkerCount() == configured);

    actor.Stop();
    CHECK(WaitForLiveActors(baseline));
}

// ---------------------------------------------------------------------------

int main() {
    TestDefaultWorkerCount();
    TestRunsWithoutConfiguration();

    return ReportResult();
}
