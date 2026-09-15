#pragma once

// thread-local bump allocator + refcounted chunk 기반 송신 버퍼.
//
//   Reserve(capacity) → SendBuffer (이동 전용, mutable)
//                     → Commit(actualSize) → SendView (복사 가능, immutable)
//
// 설계 결정과 검토했다 버린 대안은 design/adr/0001~0006 참고.

#include <cstddef>
#include <cstdint>
#include <memory>

namespace MyUtils::Network {

	class SendBufferChunk;
	class SendBuffer;
	class SendView;

	// chunk 수명 관리 수단을 한 곳으로 격리한다. ADR-0003
	using ChunkRef = std::shared_ptr<SendBufferChunk>;

	// -----------------------------------------------------------------------
	// 정책 상수
	//
	//   전부 튜닝 값이며 correctness와 무관하다.
	//   SnapshotStats()의 이용률/chunk 수/large 비율을 보고 조정한다.
	// -----------------------------------------------------------------------

	inline constexpr std::size_t DEFAULT_CHUNK_CAPACITY = 16 * 1024;

	// 이 값을 넘는 요청은 current chunk를 교체하지 않고 전용 chunk로 보낸다.
	// CAPACITY보다 작게 둘 수 있다. 큰 요청 하나가 current chunk에 남은 공간을
	// 통째로 버리게 하고 싶지 않으면 낮춘다.
	inline constexpr std::size_t LARGE_ALLOCATION_THRESHOLD = DEFAULT_CHUNK_CAPACITY;

	static_assert(LARGE_ALLOCATION_THRESHOLD <= DEFAULT_CHUNK_CAPACITY,
		"threshold가 capacity보다 크면 그 사이 크기를 담을 chunk가 없다");

	// 단일 요청의 절대 상한. 프로토콜 계층의 semantic limit와는 별개다. ADR-0006
	inline constexpr std::size_t MAX_SEND_BUFFER_SIZE = 1 * 1024 * 1024;

	// Pool이 캐싱할 유휴 chunk 개수. 넘겨 반환된 chunk는 실제로 해제된다.
	inline constexpr std::size_t DEFAULT_POOL_IDLE_LIMIT = 64;

	// -----------------------------------------------------------------------
	// SendBufferChunk
	//
	//   연속된 byte 영역 하나를 소유하고 bump allocation으로 잘라준다.
	//
	//   [주의] _offset은 동기화하지 않는다. 한 시점에 하나의 owner thread만
	//   allocation하며, 소유권 이전은 반드시 ChunkPool을 경유해야 한다.
	//   Pool을 건너뛰고 worker 사이에서 chunk를 직접 넘기면 happens-before가
	//   사라져 이 전제가 깨진다. ADR-0001, ADR-0004
	// -----------------------------------------------------------------------
	class SendBufferChunk {
	public:
		explicit SendBufferChunk(std::size_t capacity);
		~SendBufferChunk();

		SendBufferChunk(const SendBufferChunk&) = delete;
		SendBufferChunk& operator=(const SendBufferChunk&) = delete;

		// 새 owner가 사용을 시작하기 직전에 호출한다. Pool 반환 시점이 아니다.
		void Reset() noexcept;

		std::size_t Capacity() const noexcept { return _capacity; }
		std::size_t Offset() const noexcept { return _offset; }
		std::size_t FreeSize() const noexcept { return _capacity - _offset; }

		std::byte* Begin() noexcept { return _storage.get(); }
		const std::byte* Begin() const noexcept { return _storage.get(); }

	private:
		friend class SendBuffer;
		friend class SendBufferManager;

		// owner thread 전용. 공간이 부족하면 nullptr.
		std::byte* Bump(std::size_t size) noexcept;

		// tail reclaim. 호출 가능 조건 검사는 호출자(SendBuffer)가 한다.
		void Rewind(std::size_t offset) noexcept;

		std::unique_ptr<std::byte[]> _storage;
		std::size_t _capacity = 0;
		std::size_t _offset = 0;
	};

	// -----------------------------------------------------------------------
	// SendView
	//
	//   Commit으로 크기가 확정된 읽기 전용 영역. 복사 가능하며, 살아 있는 동안
	//   소유 chunk의 수명을 붙잡는다. ADR-0002
	// -----------------------------------------------------------------------
	class SendView {
	public:
		SendView() noexcept = default;

		bool IsValid() const noexcept { return _chunk != nullptr; }
		explicit operator bool() const noexcept { return IsValid(); }

		// const를 유지한다. WSABUF::buf처럼 낡은 C API 때문에 필요한 const_cast는
		// I/O 어댑터 한 곳에만 두고 그 위로는 const를 지킬 것.
		const std::byte* Data() const noexcept { return _data; }
		std::size_t Size() const noexcept { return _size; }

	private:
		friend class SendBuffer;
		SendView(ChunkRef chunk, const std::byte* data, std::size_t size) noexcept;

		ChunkRef _chunk;
		const std::byte* _data = nullptr;
		std::size_t _size = 0;
	};

	// -----------------------------------------------------------------------
	// SendBuffer
	//
	//   아직 크기가 확정되지 않은 쓰기 가능 예약 영역. 이동 전용이다.
	//   Commit이 이 객체를 소비하고 SendView를 만든다. ADR-0002
	//
	//   Commit 없이 소멸하면 예약은 가능한 경우 자동으로 되돌아간다.
	//   에러 경로에서 그냥 return해도 chunk 공간이 새지 않는다.
	// -----------------------------------------------------------------------
	class SendBuffer {
	public:
		SendBuffer() noexcept = default;
		~SendBuffer();

		SendBuffer(SendBuffer&& other) noexcept;
		SendBuffer& operator=(SendBuffer&& other) noexcept;
		SendBuffer(const SendBuffer&) = delete;
		SendBuffer& operator=(const SendBuffer&) = delete;

		bool IsValid() const noexcept { return _chunk != nullptr; }
		explicit operator bool() const noexcept { return IsValid(); }

		// 무효 버퍼에 대한 호출은 Reserve 반환값을 확인하지 않았다는 뜻이므로
		// 계약 위반으로 처리한다(ASSERT_CRASH).
		std::byte* WritableData() noexcept;

		std::size_t Capacity() const noexcept { return _capacity; }

		// 실제로 기록한 크기를 확정하고 전송용 SendView를 만든다.
		// 이 SendBuffer는 소비되어 빈 상태가 되므로 두 번 호출할 수 없다.
		SendView Commit(std::size_t writeSize) &&;

	private:
		friend class SendBufferManager;
		SendBuffer(ChunkRef chunk, std::size_t offset, std::size_t capacity) noexcept;

		// 되돌릴 수 있는 조건이면 예약 꼬리를 회수한다. Commit과 소멸자가 공유한다.
		void TryRewind(std::size_t usedSize) noexcept;
		void Clear() noexcept;

		ChunkRef _chunk;
		std::size_t _offset = 0;     // chunk 내 시작 위치
		std::size_t _capacity = 0;   // 예약 크기
	};

	// -----------------------------------------------------------------------
	// SendBufferManager
	//
	//   worker thread 하나가 소유하는 allocation front-end.
	//
	//   [주의] Current Chunk에 대한 ChunkRef를 하나 들고 있다. 이 참조를 없애면
	//   "refcount == 0"이 "아무도 이 chunk를 모른다"와 어긋나면서, 마지막 I/O가
	//   끝나는 순간 사용 중인 chunk가 Pool로 반환되어 두 worker가 같은 chunk에
	//   동시에 bump allocation한다. 제거하지 말 것. ADR-0001
	// -----------------------------------------------------------------------
	class SendBufferManager {
	public:
		// 호출 thread 전용 인스턴스.
		static SendBufferManager& Current() noexcept;

		SendBufferManager() noexcept = default;
		~SendBufferManager() = default;

		SendBufferManager(const SendBufferManager&) = delete;
		SendBufferManager& operator=(const SendBufferManager&) = delete;

		// capacity만큼 예약한다. 실제 크기는 나중에 Commit으로 확정한다.
		// 크기를 이미 아는 경우는 Reserve(N) / Commit(N)이라는 특수 사례다. ADR-0005
		//
		// capacity == 0 이거나 MAX_SEND_BUFFER_SIZE를 넘으면 무효 SendBuffer를
		// 돌려준다. 반환값은 반드시 확인해야 한다. ADR-0006
		[[nodiscard]] SendBuffer Reserve(std::size_t capacity);

		// 명시적으로 Current Chunk 참조를 놓는다. thread 종료 시에는 이 객체의
		// 소멸자가 알아서 처리하므로 호출하지 않아도 된다.
		void ReleaseCurrentChunk() noexcept;

		const ChunkRef& CurrentChunk() const noexcept { return _current; }

	private:
		ChunkRef _current;
	};

	// -----------------------------------------------------------------------
	// 계측
	//
	//   hot path는 자기 thread의 블록만 만지므로 동기화가 없다.
	//   블록의 생성자/소멸자가 등록과 집계를 처리하므로, thread가 죽어도
	//   누적값을 잃지 않고 별도 정리 훅도 필요 없다.
	// -----------------------------------------------------------------------
	struct SendBufferStats {
		std::uint64_t reserveCount = 0;
		std::uint64_t reserveFailCount = 0;    // 크기 0 또는 ceiling 초과
		std::uint64_t reservedBytes = 0;
		std::uint64_t committedBytes = 0;      // committed/reserved = 이용률
		std::uint64_t reclaimedBytes = 0;      // tail reclaim으로 되돌린 양
		std::uint64_t discardedBytes = 0;      // chunk 교체 시 버린 tail
		std::uint64_t largeAllocCount = 0;     // 전용 chunk로 간 요청
		std::uint64_t chunkAcquireCount = 0;
		std::uint64_t chunkCreateCount = 0;    // pool miss
	};

	// 모든 thread의 값을 합산한 스냅샷. 이미 종료된 thread의 누적분도 포함한다.
	//
	// [주의] 살아 있는 thread의 카운터를 동기화 없이 읽으므로 엄밀히는 data race다.
	// 통계 목적이라 허용한다. 정확한 값이 필요하면 대상 thread를 join한 뒤 읽을 것.
	SendBufferStats SnapshotStats();

	// 살아 있는 chunk 수. Pool에 들어 있는 유휴 chunk도 포함한다.
	std::size_t LiveChunkCount() noexcept;

	std::size_t PoolIdleCountApprox() noexcept;
	void SetPoolIdleLimit(std::size_t chunkCount) noexcept;
	std::size_t PoolIdleLimit() noexcept;
}
