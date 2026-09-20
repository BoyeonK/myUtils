#include "MyUtils/SendBuffer.h"
#include "MyUtils/Assert.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#include "concurrentqueue.h"

namespace MyUtils::Network {

	// =======================================================================
	// 계측
	// =======================================================================
	namespace {

		std::mutex& StatsLock() {
			static std::mutex lock;
			return lock;
		}

		// 살아 있는 thread들의 블록. StatsLock으로 보호한다.
		std::vector<SendBufferStats*>& StatsRegistry() {
			static std::vector<SendBufferStats*> registry;
			return registry;
		}

		// 이미 종료된 thread들의 누적분. StatsLock으로 보호한다.
		SendBufferStats& RetiredStats() {
			static SendBufferStats retired;
			return retired;
		}

		void AccumulateInto(SendBufferStats& dst, const SendBufferStats& src) {
			dst.reserveCount += src.reserveCount;
			dst.reserveFailCount += src.reserveFailCount;
			dst.reservedBytes += src.reservedBytes;
			dst.committedBytes += src.committedBytes;
			dst.reclaimedBytes += src.reclaimedBytes;
			dst.discardedBytes += src.discardedBytes;
			dst.largeAllocCount += src.largeAllocCount;
			dst.chunkAcquireCount += src.chunkAcquireCount;
			dst.chunkCreateCount += src.chunkCreateCount;
		}

		// 생성자가 등록하고 소멸자가 누적값을 접어 넣는다.
		// 어느 thread에서 쓰이든 자동으로 처리된다.
		class ThreadStatsBlock {
		public:
			ThreadStatsBlock() {
				std::lock_guard<std::mutex> guard(StatsLock());
				StatsRegistry().push_back(&_values);
			}

			~ThreadStatsBlock() {
				std::lock_guard<std::mutex> guard(StatsLock());
				std::vector<SendBufferStats*>& registry = StatsRegistry();
				registry.erase(
					std::remove(registry.begin(), registry.end(), &_values),
					registry.end());
				AccumulateInto(RetiredStats(), _values);
			}

			ThreadStatsBlock(const ThreadStatsBlock&) = delete;
			ThreadStatsBlock& operator=(const ThreadStatsBlock&) = delete;

			SendBufferStats& Values() noexcept { return _values; }

		private:
			SendBufferStats _values;
		};

		SendBufferStats& Stats() noexcept {
			thread_local ThreadStatsBlock block;
			return block.Values();
		}

		std::atomic<std::size_t> GLiveChunkCount{ 0 };
		std::atomic<std::size_t> GLiveChunkBytes{ 0 };
	}

	SendBufferStats SnapshotStats() {
		std::lock_guard<std::mutex> guard(StatsLock());

		SendBufferStats total = RetiredStats();
		for (const SendBufferStats* block : StatsRegistry())
			AccumulateInto(total, *block);

		return total;
	}

	std::size_t LiveChunkCount() noexcept {
		return GLiveChunkCount.load(std::memory_order_relaxed);
	}

	std::size_t LiveChunkBytes() noexcept {
		return GLiveChunkBytes.load(std::memory_order_relaxed);
	}

	// =======================================================================
	// ChunkPool
	//
	//   유휴 chunk를 보관하는 프로세스 전역 풀.
	//
	//   [주의] chunk deleter가 pool의 소유권 지분을 캡처한다. pool이 자기가 내준
	//   chunk 전부보다 오래 살아야 반환이 안전하기 때문이다. 이 캡처를 없애면
	//   종료 시점에 이미 파괴된 pool로 반환하는 경로가 생긴다.
	//
	//   유휴 chunk는 raw 포인터로만 보관하므로 순환 참조는 생기지 않는다.
	// =======================================================================
	namespace {

		class ChunkPool : public std::enable_shared_from_this<ChunkPool> {
		public:
			~ChunkPool() {
				SendBufferChunk* chunk = nullptr;
				while (_idle.try_dequeue(chunk))
					delete chunk;
			}

			ChunkRef Acquire();

			void SetIdleLimit(std::size_t limit) noexcept {
				_idleLimit.store(limit, std::memory_order_relaxed);
			}

			std::size_t IdleLimit() const noexcept {
				return _idleLimit.load(std::memory_order_relaxed);
			}

			std::size_t IdleCountApprox() const noexcept {
				return _idle.size_approx();
			}

		private:
			void Return(SendBufferChunk* chunk) noexcept;

			moodycamel::ConcurrentQueue<SendBufferChunk*> _idle;
			std::atomic<std::size_t> _idleLimit{ DEFAULT_POOL_IDLE_LIMIT };
		};

		using PoolRef = std::shared_ptr<ChunkPool>;

		// 프로세스 수명 동안 pool을 붙잡는 루트 참조.
		// 이 참조가 사라져도 아직 나가 있는 chunk들이 pool을 살려둔다.
		PoolRef& GlobalPoolRef() {
			static PoolRef root = std::make_shared<ChunkPool>();
			return root;
		}

		ChunkPool& GlobalPool() { return *GlobalPoolRef(); }

		ChunkRef ChunkPool::Acquire() {
			SendBufferChunk* raw = nullptr;
			if (!_idle.try_dequeue(raw)) {
				raw = new SendBufferChunk(DEFAULT_CHUNK_CAPACITY);
				Stats().chunkCreateCount += 1;
			}

			// 새 owner가 쓰기 직전에 초기화한다.
			// 큐의 enqueue/dequeue가 happens-before를 만들어주므로, 이전 owner가
			// 남긴 _offset 값을 동기화 없이 안전하게 덮어쓸 수 있다.
			raw->Reset();

			PoolRef self = shared_from_this();
			return ChunkRef(raw, [self](SendBufferChunk* chunk) noexcept {
				self->Return(chunk);
			});
		}

		void ChunkPool::Return(SendBufferChunk* chunk) noexcept {
			if (chunk == nullptr)
				return;

			// 일반 크기가 아닌 chunk는 캐싱하지 않는다. 지금은 large allocation이
			// pool을 거치지 않으므로 방어적 검사다.
			const bool poolable = (chunk->Capacity() == DEFAULT_CHUNK_CAPACITY);

			if (!poolable || _idle.size_approx() >= IdleLimit()) {
				delete chunk;
				return;
			}

			if (!_idle.enqueue(chunk))
				delete chunk;
		}
	}

	std::size_t PoolIdleCountApprox() noexcept {
		return GlobalPool().IdleCountApprox();
	}

	void SetPoolIdleLimit(std::size_t chunkCount) noexcept {
		GlobalPool().SetIdleLimit(chunkCount);
	}

	std::size_t PoolIdleLimit() noexcept {
		return GlobalPool().IdleLimit();
	}

	// =======================================================================
	// SendBufferChunk
	// =======================================================================

	SendBufferChunk::SendBufferChunk(std::size_t capacity)
		: _capacity(capacity) {
		// 할당보다 먼저 검사한다. 초기화 리스트에서 할당하면 검사가 그 뒤로 밀린다.
		MYUTILS_ASSERT(capacity > 0);

		// 기본 초기화다. zero-fill하지 않는다. 내용은 쓰기 전까지 의미가 없다.
		_storage.reset(new std::byte[capacity]);

		GLiveChunkCount.fetch_add(1, std::memory_order_relaxed);
		GLiveChunkBytes.fetch_add(capacity, std::memory_order_relaxed);
	}

	SendBufferChunk::~SendBufferChunk() {
		GLiveChunkCount.fetch_sub(1, std::memory_order_relaxed);
		GLiveChunkBytes.fetch_sub(_capacity, std::memory_order_relaxed);
	}

	void SendBufferChunk::Reset() noexcept {
		_offset = 0;
	}

	std::byte* SendBufferChunk::Bump(std::size_t size) noexcept {
		// _offset <= _capacity가 항상 성립하므로 뺄셈이 underflow하지 않는다.
		if (size > _capacity - _offset)
			return nullptr;

		std::byte* result = _storage.get() + _offset;
		_offset += size;
		return result;
	}

	void SendBufferChunk::Rewind(std::size_t offset) noexcept {
		MYUTILS_ASSERT(offset <= _offset);
		_offset = offset;
	}

	// =======================================================================
	// SendView
	// =======================================================================

	SendView::SendView(ChunkRef chunk, const std::byte* data, std::size_t size) noexcept
		: _chunk(std::move(chunk))
		, _data(data)
		, _size(size) {
	}

	// =======================================================================
	// SendBuffer
	// =======================================================================

	SendBuffer::SendBuffer(ChunkRef chunk, std::size_t offset, std::size_t capacity) noexcept
		: _chunk(std::move(chunk))
		, _offset(offset)
		, _capacity(capacity) {
	}

	SendBuffer::SendBuffer(SendBuffer&& other) noexcept
		: _chunk(std::move(other._chunk))
		, _offset(other._offset)
		, _capacity(other._capacity) {
		other.Clear();
	}

	SendBuffer& SendBuffer::operator=(SendBuffer&& other) noexcept {
		if (this != &other) {
			TryRewind(0);   // 내가 들고 있던 예약을 먼저 되돌린다

			_chunk = std::move(other._chunk);
			_offset = other._offset;
			_capacity = other._capacity;
			other.Clear();
		}
		return *this;
	}

	SendBuffer::~SendBuffer() {
		// Commit 없이 버려진 예약을 회수한다.
		// Commit을 거쳤다면 _chunk가 이미 비어 있어 아무 일도 하지 않는다.
		TryRewind(0);
	}

	void SendBuffer::Clear() noexcept {
		_chunk.reset();
		_offset = 0;
		_capacity = 0;
	}

	std::byte* SendBuffer::WritableData() noexcept {
		MYUTILS_ASSERT(_chunk != nullptr);
		return _chunk->Begin() + _offset;
	}

	void SendBuffer::TryRewind(std::size_t usedSize) noexcept {
		if (_chunk == nullptr)
			return;

		// 두 조건은 서로 다른 것을 증명하므로 둘 다 필요하다. 하나만 빼도 깨진다.
		//   1. Current == 내 chunk : 내가 owner다. _offset을 써도 레이스가 없다
		//   2. 커서 == 내 끝        : 내 뒤에 할당이 없다. 남의 영역을 안 침범한다
		// 1번이 owner임을 증명하는 이유는 chunk가 한 시점에 최대 하나의 manager에게만
		// Current일 수 있기 때문이다 — manager가 참조를 들고 있어 Pool이 다른 worker
		// 에게 내줄 수 없다. 그래서 thread id 비교도 atomic도 필요 없다.
		if (SendBufferManager::Current().CurrentChunk().get() != _chunk.get())
			return;
		if (_chunk->Offset() != _offset + _capacity)
			return;

		_chunk->Rewind(_offset + usedSize);
		Stats().reclaimedBytes += (_capacity - usedSize);
	}

	SendView SendBuffer::Commit(std::size_t writeSize) && {
		MYUTILS_ASSERT(_chunk != nullptr);
		MYUTILS_ASSERT(writeSize <= _capacity);

		TryRewind(writeSize);

		const std::byte* data = _chunk->Begin() + _offset;
		SendView view(std::move(_chunk), data, writeSize);

		Stats().committedBytes += writeSize;

		// 이 객체는 소비되었다. 이중 Commit과 소멸자의 중복 회수를 함께 막는다.
		Clear();
		return view;
	}

	// =======================================================================
	// SendBufferManager
	// =======================================================================

	SendBufferManager& SendBufferManager::Current() noexcept {
		thread_local SendBufferManager manager;
		return manager;
	}

	void SendBufferManager::ReleaseCurrentChunk() noexcept {
		_current.reset();
	}

	SendBuffer SendBufferManager::Reserve(std::size_t capacity) {
		SendBufferStats& stats = Stats();
		stats.reserveCount += 1;

		if (capacity == 0 || capacity > MAX_SEND_BUFFER_SIZE) {
			stats.reserveFailCount += 1;
			return SendBuffer{};
		}

		stats.reservedBytes += capacity;

		// 임계값을 넘는 요청은 current chunk를 교체하지 않고 전용 chunk로 간다.
		if (capacity > LARGE_ALLOCATION_THRESHOLD) {
			stats.largeAllocCount += 1;

			ChunkRef solo = std::make_shared<SendBufferChunk>(capacity);
			const std::byte* data = solo->Bump(capacity);
			MYUTILS_ASSERT(data != nullptr);
			return SendBuffer(std::move(solo), 0, capacity);
		}

		if (_current != nullptr) {
			const std::size_t offset = _current->Offset();
			if (_current->Bump(capacity) != nullptr)
				return SendBuffer(_current, offset, capacity);

			stats.discardedBytes += _current->FreeSize();
		}

		// 남은 공간이 모자라다. chunk를 교체한다.
		//
		// [주의] "교체 먼저, 이전 참조 해제 나중" 순서를 지켜야 한다. shared_ptr의
		// 이동 대입은 새 값을 자리에 넣은 뒤 옛 값을 파괴하므로 이 순서가 보장된다.
		// 이전 chunk를 먼저 놓으면 그 사이 pool로 반환되어 잠시 dangling이 된다.
		_current = GlobalPool().Acquire();
		stats.chunkAcquireCount += 1;

		// 갓 Reset된 chunk이고 capacity <= LARGE_ALLOCATION_THRESHOLD <=
		// DEFAULT_CHUNK_CAPACITY이므로 반드시 성공한다.
		std::byte* data = _current->Bump(capacity);
		MYUTILS_ASSERT(data != nullptr);
		return SendBuffer(_current, 0, capacity);
	}
}
