// SendBuffer correctness test
//
// 의존성 없는 단일 실행 파일이다. 실패 개수를 종료 코드로 돌려주므로
// ctest가 그대로 판정한다.

#include "MyUtils/SendBuffer.h"

#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace MyUtils::Network;

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

// 버퍼를 seed에서 파생된 패턴으로 채우고 검증한다. 영역이 겹치면 깨진다.
static void Fill(std::byte* p, std::size_t n, std::uint32_t seed) {
    for (std::size_t i = 0; i < n; ++i)
        p[i] = static_cast<std::byte>((seed * 31u + static_cast<std::uint32_t>(i) * 17u) & 0xFF);
}

static bool Verify(const std::byte* p, std::size_t n, std::uint32_t seed) {
    for (std::size_t i = 0; i < n; ++i) {
        const auto expected =
            static_cast<std::byte>((seed * 31u + static_cast<std::uint32_t>(i) * 17u) & 0xFF);
        if (p[i] != expected)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

static void TestBasic() {
    Section("basic reserve / commit");
    auto& mgr = SendBufferManager::Current();

    SendBuffer buf = mgr.Reserve(256);
    CHECK(buf.IsValid());
    CHECK(buf.Capacity() == 256);

    Fill(buf.WritableData(), 256, 1);
    SendView view = std::move(buf).Commit(200);

    CHECK(view.IsValid());
    CHECK(view.Size() == 200);
    CHECK(Verify(view.Data(), 200, 1));

    // Commit이 원본을 소비한다 (이중 Commit 방지)
    CHECK(!buf.IsValid());
    CHECK(buf.Capacity() == 0);
}

static void TestInvalidInputs() {
    Section("invalid inputs");
    auto& mgr = SendBufferManager::Current();

    SendBuffer zero = mgr.Reserve(0);
    CHECK(!zero.IsValid());

    SendBuffer tooBig = mgr.Reserve(MAX_SEND_BUFFER_SIZE + 1);
    CHECK(!tooBig.IsValid());

    SendBuffer atLimit = mgr.Reserve(MAX_SEND_BUFFER_SIZE);
    CHECK(atLimit.IsValid());
}

static void TestTailReclaim() {
    Section("tail reclaim");
    auto& mgr = SendBufferManager::Current();

    // current chunk 확보
    { SendView v = std::move(mgr.Reserve(16)).Commit(16); }
    CHECK(mgr.CurrentChunk() != nullptr);

    // (1) 마지막 allocation이면 꼬리를 회수한다
    {
        const std::size_t before = mgr.CurrentChunk()->Offset();
        SendBuffer buf = mgr.Reserve(1024);
        CHECK(mgr.CurrentChunk()->Offset() == before + 1024);
        SendView v = std::move(buf).Commit(100);
        CHECK(mgr.CurrentChunk()->Offset() == before + 100);
    }

    // (2) 뒤에 다른 allocation이 있으면 회수하지 않는다
    {
        SendBuffer first = mgr.Reserve(1000);
        SendBuffer second = mgr.Reserve(200);
        const std::size_t before = mgr.CurrentChunk()->Offset();

        SendView v1 = std::move(first).Commit(10);
        CHECK(mgr.CurrentChunk()->Offset() == before);   // 회수 불가

        // second는 마지막이므로 회수된다
        SendView v2 = std::move(second).Commit(20);
        CHECK(mgr.CurrentChunk()->Offset() == before - 180);

        // 두 영역이 겹치지 않는다
        CHECK(v1.Data() + 1000 == v2.Data());
    }

    // (3) Commit 없이 소멸하면 예약 전체가 되돌아간다
    {
        const std::size_t before = mgr.CurrentChunk()->Offset();
        { SendBuffer dropped = mgr.Reserve(500); }
        CHECK(mgr.CurrentChunk()->Offset() == before);
    }

    // (4) Commit(0)은 유효한 크기 0짜리 view
    {
        const std::size_t before = mgr.CurrentChunk()->Offset();
        SendView v = std::move(mgr.Reserve(64)).Commit(0);
        CHECK(v.IsValid());
        CHECK(v.Size() == 0);
        CHECK(mgr.CurrentChunk()->Offset() == before);
    }
}

static void TestMoveSemantics() {
    Section("move semantics");
    auto& mgr = SendBufferManager::Current();
    { SendView v = std::move(mgr.Reserve(16)).Commit(16); }

    // 이동해도 예약이 두 번 되돌아가지 않는다
    {
        const std::size_t before = mgr.CurrentChunk()->Offset();
        {
            SendBuffer a = mgr.Reserve(300);
            SendBuffer b = std::move(a);
            CHECK(!a.IsValid());
            CHECK(b.IsValid());
            CHECK(b.Capacity() == 300);
        }
        CHECK(mgr.CurrentChunk()->Offset() == before);
    }

    // 이동 대입은 대상이 들고 있던 예약을 먼저 되돌리려 시도한다.
    // 단 a 뒤에 b가 있으므로 a의 300은 LIFO 조건에 걸려 회수되지 않는다.
    // 최종적으로 b의 100만 회수되어 offset은 before+300에 머문다.
    {
        const std::size_t before = mgr.CurrentChunk()->Offset();
        {
            SendBuffer a = mgr.Reserve(300);
            SendBuffer b = mgr.Reserve(100);
            a = std::move(b);
            CHECK(a.Capacity() == 100);
            CHECK(!b.IsValid());
        }
        CHECK(mgr.CurrentChunk()->Offset() == before + 300);
    }
}

static void TestChunkSwap() {
    Section("chunk swap / lifetime");
    auto& mgr = SendBufferManager::Current();
    mgr.ReleaseCurrentChunk();

    SendBuffer first = mgr.Reserve(1024);
    ChunkRef firstChunk = mgr.CurrentChunk();
    CHECK(firstChunk != nullptr);
    Fill(first.WritableData(), 1024, 7);
    SendView keepAlive = std::move(first).Commit(1024);

    // chunk를 가득 채워 교체를 유발한다
    std::vector<SendView> views;
    while (mgr.CurrentChunk() == firstChunk) {
        SendBuffer b = mgr.Reserve(1024);
        CHECK(b.IsValid());
        views.push_back(std::move(b).Commit(1024));
    }
    CHECK(mgr.CurrentChunk() != firstChunk);

    // 이전 chunk는 outstanding view 때문에 살아 있다
    CHECK(firstChunk.use_count() >= 2);
    CHECK(Verify(keepAlive.Data(), 1024, 7));
}

static void TestLargeAllocation() {
    Section("large allocation");
    auto& mgr = SendBufferManager::Current();
    { SendView v = std::move(mgr.Reserve(16)).Commit(16); }

    ChunkRef before = mgr.CurrentChunk();
    const std::size_t offsetBefore = before->Offset();

    SendBuffer big = mgr.Reserve(LARGE_ALLOCATION_THRESHOLD + 1);
    CHECK(big.IsValid());
    CHECK(big.Capacity() == LARGE_ALLOCATION_THRESHOLD + 1);

    // 전용 chunk로 갔으므로 current chunk가 보존된다
    CHECK(mgr.CurrentChunk() == before);
    CHECK(mgr.CurrentChunk()->Offset() == offsetBefore);

    Fill(big.WritableData(), 4096, 9);
    SendView v = std::move(big).Commit(4096);
    CHECK(Verify(v.Data(), 4096, 9));

    // 큰 요청 뒤에도 작은 할당은 원래 chunk를 계속 쓴다
    SendView small = std::move(mgr.Reserve(32)).Commit(32);
    CHECK(mgr.CurrentChunk() == before);
}

static void TestPoolReturn() {
    Section("pool return");
    auto& mgr = SendBufferManager::Current();
    mgr.ReleaseCurrentChunk();

    {
        SendBuffer b = mgr.Reserve(128);
        // 획득 자체가 pool에서 하나를 꺼낼 수 있으므로 기준점은 획득 이후에 잡는다
        const std::size_t idle = PoolIdleCountApprox();

        SendView v = std::move(b).Commit(128);
        mgr.ReleaseCurrentChunk();
        // view가 살아 있으므로 아직 pool로 돌아가지 않는다
        CHECK(PoolIdleCountApprox() == idle);

        v = SendView{};
        // 마지막 view가 사라지면 반환된다
        CHECK(PoolIdleCountApprox() == idle + 1);
    }

    // 재사용 시 offset이 초기화되어 있다
    SendBuffer reused = mgr.Reserve(64);
    CHECK(reused.IsValid());
    CHECK(mgr.CurrentChunk()->Offset() == 64);
}

static void TestCrossThreadRelease() {
    Section("cross-thread release");
    auto& mgr = SendBufferManager::Current();
    mgr.ReleaseCurrentChunk();

    SendBuffer b = mgr.Reserve(256);
    const std::size_t idle = PoolIdleCountApprox();

    SendView view = std::move(b).Commit(256);
    mgr.ReleaseCurrentChunk();
    CHECK(PoolIdleCountApprox() == idle);

    // 할당한 thread가 아닌 다른 thread에서 마지막 참조를 놓는다
    std::thread releaser([v = std::move(view)]() mutable {
        CHECK(v.IsValid());
        v = SendView{};
    });
    releaser.join();

    CHECK(PoolIdleCountApprox() == idle + 1);
}

static void TestViewCopy() {
    Section("view copy (broadcast)");
    auto& mgr = SendBufferManager::Current();
    mgr.ReleaseCurrentChunk();

    SendBuffer b = mgr.Reserve(512);
    const std::size_t idle = PoolIdleCountApprox();

    std::vector<SendView> fanout;
    {
        Fill(b.WritableData(), 512, 11);
        SendView origin = std::move(b).Commit(512);
        for (int i = 0; i < 100; ++i)
            fanout.push_back(origin);       // 복사 = chunk refcount 증가
    }
    mgr.ReleaseCurrentChunk();

    CHECK(PoolIdleCountApprox() == idle);   // 100개가 붙잡고 있다
    for (const SendView& v : fanout)
        CHECK(Verify(v.Data(), 512, 11));

    fanout.clear();
    CHECK(PoolIdleCountApprox() == idle + 1);
}

// ---------------------------------------------------------------------------
// 다중 thread 스트레스: 생산자들이 할당/커밋하고, 소비자가 다른 thread에서
// 내용을 검증한 뒤 해제한다. 영역이 겹치면 패턴 검증이 깨진다.
// ---------------------------------------------------------------------------
static void TestStress() {
    Section("multi-thread stress");

    struct Item {
        SendView view;
        std::uint32_t seed = 0;
        std::size_t size = 0;
    };

    std::mutex lock;
    std::deque<Item> queue;
    std::atomic<bool> done{ false };
    std::atomic<int> verified{ 0 };
    std::atomic<int> corrupted{ 0 };

    constexpr int PRODUCERS = 4;
    constexpr int CONSUMERS = 3;
    constexpr int PER_PRODUCER = 4000;

    std::vector<std::thread> threads;

    for (int p = 0; p < PRODUCERS; ++p) {
        threads.emplace_back([&, p] {
            std::mt19937 rng(static_cast<std::uint32_t>(p) + 1234u);
            std::uniform_int_distribution<int> sizeDist(1, 3000);

            for (int i = 0; i < PER_PRODUCER; ++i) {
                const std::size_t reserved = static_cast<std::size_t>(sizeDist(rng));
                // 절반은 과다 예약 후 축소 커밋 (tail reclaim 경로)
                const std::size_t used = (i % 2 == 0) ? reserved : (reserved / 2 + 1);
                const std::uint32_t seed = static_cast<std::uint32_t>(p * 1000003 + i);

                SendBuffer buf = SendBufferManager::Current().Reserve(reserved);
                if (!buf.IsValid()) { ++corrupted; continue; }

                Fill(buf.WritableData(), used, seed);
                SendView view = std::move(buf).Commit(used);

                std::lock_guard<std::mutex> guard(lock);
                queue.push_back(Item{ std::move(view), seed, used });
            }
            SendBufferManager::Current().ReleaseCurrentChunk();
        });
    }

    for (int c = 0; c < CONSUMERS; ++c) {
        threads.emplace_back([&] {
            for (;;) {
                Item item;
                {
                    std::lock_guard<std::mutex> guard(lock);
                    if (queue.empty()) {
                        if (done.load()) return;
                        continue;
                    }
                    item = std::move(queue.front());
                    queue.pop_front();
                }
                if (Verify(item.view.Data(), item.size, item.seed))
                    ++verified;
                else
                    ++corrupted;
                // item 소멸 = 할당하지 않은 thread에서의 release
            }
        });
    }

    for (int p = 0; p < PRODUCERS; ++p)
        threads[static_cast<std::size_t>(p)].join();
    done.store(true);
    for (std::size_t i = PRODUCERS; i < threads.size(); ++i)
        threads[i].join();

    std::printf("  verified=%d corrupted=%d\n", verified.load(), corrupted.load());
    CHECK(corrupted.load() == 0);
    CHECK(verified.load() == PRODUCERS * PER_PRODUCER);
}

int main() {
    // 반환된 chunk가 상한에 걸려 해제되지 않도록 넉넉히 올려둔다.
    // 그래야 pool 관련 단언이 결정적으로 동작한다.
    SetPoolIdleLimit(1000000);

    TestBasic();
    TestInvalidInputs();
    TestTailReclaim();
    TestMoveSemantics();
    TestChunkSwap();
    TestLargeAllocation();
    TestPoolReturn();
    TestCrossThreadRelease();
    TestViewCopy();
    TestStress();

    SendBufferManager::Current().ReleaseCurrentChunk();

    const SendBufferStats stats = SnapshotStats();
    std::printf("\n--- stats ---\n");
    std::printf("  reserve        : %llu (fail %llu)\n",
        (unsigned long long)stats.reserveCount, (unsigned long long)stats.reserveFailCount);
    std::printf("  reserved bytes : %llu\n", (unsigned long long)stats.reservedBytes);
    std::printf("  committed bytes: %llu\n", (unsigned long long)stats.committedBytes);
    std::printf("  reclaimed bytes: %llu\n", (unsigned long long)stats.reclaimedBytes);
    std::printf("  discarded bytes: %llu\n", (unsigned long long)stats.discardedBytes);
    std::printf("  large allocs   : %llu\n", (unsigned long long)stats.largeAllocCount);
    std::printf("  chunk acquire  : %llu (created %llu)\n",
        (unsigned long long)stats.chunkAcquireCount, (unsigned long long)stats.chunkCreateCount);
    if (stats.reservedBytes > 0) {
        std::printf("  utilization    : %.1f%% (committed/reserved)\n",
            100.0 * double(stats.committedBytes) / double(stats.reservedBytes));
    }
    std::printf("  live chunks    : %zu / %zu bytes (pool idle %zu)\n",
        LiveChunkCount(), LiveChunkBytes(), PoolIdleCountApprox());

    std::printf("\n%s  (%d checks, %d failures)\n",
        GFailCount == 0 ? "ALL PASS" : "FAILED", GCheckCount, GFailCount);
    return GFailCount == 0 ? 0 : 1;
}
