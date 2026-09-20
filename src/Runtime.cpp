#include "RuntimeInternal.h"

#include "MyUtils/Assert.h"

#include <algorithm>
#include <chrono>
#include <utility>

// 구현 전체가 design/actor-runtime.md 의 규약을 따른다.
// 이 파일이 지는 것은 몸체와 pool이다 — Actor 쪽은 Actors.cpp에 있다.

namespace MyUtils::Runtime {

	// =======================================================================
	// 계측
	//
	//   SendBuffer와 같은 패턴이다. thread_local 블록이 자기 생성자/소멸자에서
	//   등록과 집계를 처리하므로 hot path에 동기화가 없고, 어느 thread에서
	//   쓰이든 자동으로 처리된다.
	// =======================================================================
	namespace {

		std::mutex& StatsLock() {
			static std::mutex lock;
			return lock;
		}

		std::vector<Stats*>& StatsRegistry() {
			static std::vector<Stats*> registry;
			return registry;
		}

		Stats& RetiredStats() {
			static Stats retired;
			return retired;
		}

		void AccumulateInto(Stats& dst, const Stats& src) {
			dst.sendAccepted += src.sendAccepted;
			dst.sendRejected += src.sendRejected;
			dst.scheduleCount += src.scheduleCount;
			dst.scheduleAbortedSystemStopping += src.scheduleAbortedSystemStopping;
			dst.runCount += src.runCount;
			dst.messagesHandled += src.messagesHandled;
			dst.queueWaitUsTotal += src.queueWaitUsTotal;
			dst.spawnCount += src.spawnCount;
			dst.finalizeCount += src.finalizeCount;
			dst.handlerExceptionCount += src.handlerExceptionCount;
			dst.workerSleepCount += src.workerSleepCount;
			dst.localPushCount += src.localPushCount;
			dst.injectionPushCount += src.injectionPushCount;
		}

		class ThreadStatsBlock {
		public:
			ThreadStatsBlock() {
				std::lock_guard<std::mutex> guard(StatsLock());
				StatsRegistry().push_back(&_values);
			}

			~ThreadStatsBlock() {
				std::lock_guard<std::mutex> guard(StatsLock());
				std::vector<Stats*>& registry = StatsRegistry();
				registry.erase(
					std::remove(registry.begin(), registry.end(), &_values),
					registry.end());
				AccumulateInto(RetiredStats(), _values);
			}

			ThreadStatsBlock(const ThreadStatsBlock&) = delete;
			ThreadStatsBlock& operator=(const ThreadStatsBlock&) = delete;

			Stats& Values() noexcept { return _values; }

		private:
			Stats _values;
		};

		// =======================================================================
		// worker 신원
		//
		//   Submit이 "지금 이 스레드가 worker인가"를 알아야 local deque로 보낼 수
		//   있다. Actor의 TrySchedule은 ACB 깊은 곳에서 불리므로 인자로 내려보낼
		//   수 없어 ambient 값이 필요하다.
		//
		//   worker loop가 진입할 때 스스로 세팅하고 나갈 때 지운다. 외부에서
		//   초기화 훅을 부를 필요가 없고, worker가 아닌 스레드는 isWorker가
		//   false라 자연히 injection queue로 떨어진다.
		//
		//   [주의] 과거에는 여기에 런타임 포인터도 담았다. ActorSystem이 둘 이상일
		//   때 시스템 A의 worker가 만든 B의 runnable이 A의 deque로 새는 것을
		//   막기 위해서였다. 런타임이 프로세스 전역 단일이 되면서 비교 대상이
		//   사라졌다. **전역 단일이라는 전제가 바뀌면 여기부터 되돌려야 한다.**
		// =======================================================================
		struct WorkerIdentity {
			bool isWorker = false;
			std::size_t index = 0;
		};

		WorkerIdentity& CurrentWorker() noexcept {
			thread_local WorkerIdentity identity;
			return identity;
		}

		class WorkerScope {
		public:
			explicit WorkerScope(std::size_t index) noexcept {
				WorkerIdentity& identity = CurrentWorker();
				identity.isWorker = true;
				identity.index = index;
			}

			~WorkerScope() {
				WorkerIdentity& identity = CurrentWorker();
				identity.isWorker = false;
				identity.index = 0;
			}

			WorkerScope(const WorkerScope&) = delete;
			WorkerScope& operator=(const WorkerScope&) = delete;
		};

		// =======================================================================
		// 구성
		//
		//   설정값과 기동 플래그는 **몸체 바깥**에 둔다. 몸체 안에 두면
		//   SetWorkerCount가 몸체를 만지게 되고, 그 호출 자체가 기동을 유발해
		//   API가 자기 목적을 파괴한다.
		//
		//   상수 초기화되는 atomic이라 동적 초기화 순서를 타지 않는다.
		// =======================================================================
		std::atomic<std::size_t> GDesiredWorkerCount{ 0 };   // 0 = 기본값
		std::atomic<bool> GStarted{ false };

		std::size_t ResolveWorkerCount() noexcept {
			const std::size_t configured = GDesiredWorkerCount.load();
			if (configured != 0)
				return configured;

			const unsigned int hardware = std::thread::hardware_concurrency();
			return hardware == 0 ? std::size_t{ 1 } : static_cast<std::size_t>(hardware);
		}

		// 멤버 초기화 시점에 설정을 읽고 곧바로 기동 플래그를 세운다.
		// 이후의 SetWorkerCount는 assert로 죽는다.
		std::size_t StartWorkerCount() noexcept {
			const std::size_t count = ResolveWorkerCount();
			GStarted.store(true);
			return count;
		}

	}

	Stats SnapshotStats() {
		std::lock_guard<std::mutex> guard(StatsLock());

		Stats total = RetiredStats();
		for (const Stats* block : StatsRegistry())
			AccumulateInto(total, *block);

		return total;
	}

	namespace Detail {

		Stats& ThreadStats() noexcept {
			thread_local ThreadStatsBlock block;
			return block.Values();
		}

		void TouchStatsGlobals() noexcept {
			StatsLock();
			StatsRegistry();
			RetiredStats();
		}

		std::uint64_t NowUs() noexcept {
			using namespace std::chrono;
			return static_cast<std::uint64_t>(
				duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
		}

		// =======================================================================
		// WorkerPool
		// =======================================================================

		WorkerPool::WorkerPool(std::size_t workerCount)
			: _workerCount(workerCount) {
			MYUTILS_ASSERT(workerCount >= 1);

			// 뮤텍스가 이동 불가라 unique_ptr로 담는다
			_local.reserve(workerCount);
			for (std::size_t i = 0; i < workerCount; ++i)
				_local.push_back(std::make_unique<LocalDeque>());
		}

		WorkerPool::~WorkerPool() {
			// 정상 경로에서는 RuntimeBody::Shutdown이 이미 수행했다. 안전장치다.
			StopAndJoin();
		}

		void WorkerPool::Start() {
			_workers.reserve(_workerCount);
			for (std::size_t i = 0; i < _workerCount; ++i)
				_workers.emplace_back([this, i] { WorkerLoop(i); });
		}

		bool WorkerPool::Submit(TaskPtr task) {
			MYUTILS_ASSERT(task != nullptr);

			if (_stopping.load())
				return false;

			task->enqueuedAtUs.store(NowUs(), std::memory_order_relaxed);

			// [라우팅] 이 스레드가 worker인가. worker가 실행 중에 만들어낸 task는
			// 자기 local deque로, 그 밖의 것은 injection queue로 간다. 기준은
			// locality다.
			const WorkerIdentity& self = CurrentWorker();
			if (self.isWorker) {
				{
					// [락 순서] deque 락을 놓은 뒤에 sleep 락을 잡는다. 반대로 중첩하는
					// 경로는 worker의 sleep 전 재확인(sleep -> deque)뿐이라 순환이 없다.
					std::lock_guard<std::mutex> guard(_local[self.index]->lock);
					_local[self.index]->items.push_back(std::move(task));
				}
				ThreadStats().localPushCount += 1;

				// local push의 notify는 correctness가 아니라 heuristic이다.
				// push한 주체가 이 deque의 owner이고, owner는 잠들기 전에 자기 deque를
				// 확인하므로 건너뛰어도 진행은 보장된다. 그래서 조건을 붙일 수 있다.
				//
				// 읽기는 반드시 push "이후"여야 한다. 그래야 여기서 0을 보고 건너뛴
				// 경우에 뒤이어 잠드는 worker의 재확인이 이 push를 반드시 본다.
				if (_idleCount.load() == 0)
					return true;

				{
					std::lock_guard<std::mutex> guard(_sleepLock);
				}
				_wakeup.notify_one();
				return true;
			}

			if (!_injection.enqueue(std::move(task)))
				return false;

			ThreadStats().injectionPushCount += 1;

			// injection의 notify는 correctness다. producer가 외부 스레드일 수 있고
			// worker 전부가 자고 있을 수 있어, 빠뜨리면 아무도 깨지 않는다.
			// 조건을 붙이지 않는다.
			//
			// 빈 임계구역이 장치다. worker가 "재확인 -> wait 진입" 구간에 있으면
			// 여기서 락을 얻지 못해 대기하고, worker가 wait에 들어가 락을 놓은 뒤에야
			// notify가 나간다. worker가 아직 구간에 들어오지 않았다면 재확인 시점에
			// enqueue가 이미 끝나 있어 큐에서 발견한다. design/actor-runtime.md §5
			{
				std::lock_guard<std::mutex> guard(_sleepLock);
			}
			_wakeup.notify_one();
			return true;
		}

		bool WorkerPool::PopLocal(std::size_t index, TaskPtr& out) {
			LocalDeque& deque = *_local[index];

			std::lock_guard<std::mutex> guard(deque.lock);
			if (deque.items.empty())
				return false;

			// pop_bottom: 방금 넣은 것부터. locality heuristic이다.
			out = std::move(deque.items.back());
			deque.items.pop_back();
			return true;
		}

		bool WorkerPool::TrySteal(std::size_t index, TaskPtr& out) {
			if (_workerCount <= 1)
				return false;

			// victim 선택은 단순 순회다. 최적화하지 않았다.
			for (std::size_t offset = 1; offset < _workerCount; ++offset) {
				LocalDeque& victim = *_local[(index + offset) % _workerCount];

				std::lock_guard<std::mutex> guard(victim.lock);
				if (victim.items.empty())
					continue;

				// steal_top: owner가 집는 쪽의 반대편. 가장 오래된 것부터 가져간다.
				out = std::move(victim.items.front());
				victim.items.pop_front();
				return true;
			}

			return false;
		}

		bool WorkerPool::NextTask(std::size_t index, std::size_t tick, TaskPtr& out) {
			// 주기적으로 injection을 먼저 본다. 이게 없으면 local deque가 계속 차
			// 있는 동안 injection을 한 번도 확인하지 않아 외부 스레드와 I/O
			// completion의 작업이 무한정 밀린다.
			if (tick % INJECTION_POLL_INTERVAL == 0) {
				if (_injection.try_dequeue(out))
					return true;
				if (PopLocal(index, out))
					return true;
			}
			else {
				if (PopLocal(index, out))
					return true;
				if (_injection.try_dequeue(out))
					return true;
			}

			// steal은 마지막이다. injection은 아직 아무도 안 집은 일이고 steal은 남의
			// 부하를 더는 것이라, 전자가 급하다.
			return TrySteal(index, out);
		}

		bool WorkerPool::AcquireAnywhere(std::size_t index, TaskPtr& out) {
			if (PopLocal(index, out))
				return true;
			if (_injection.try_dequeue(out))
				return true;
			return TrySteal(index, out);
		}

		void WorkerPool::WorkerLoop(std::size_t index) {
			// 진입할 때 스스로 신원을 세팅한다. 외부 초기화 훅이 없다.
			WorkerScope scope(index);

			for (std::size_t tick = 0;; ++tick) {
				if (_stopping.load())
					return;

				TaskPtr task;
				if (NextTask(index, tick, task)) {
					task->Execute();
					continue;
				}

				std::unique_lock<std::mutex> lock(_sleepLock);
				if (_stopping.load())
					return;

				// 유휴 선언은 sleep 락 안에서. 락 밖에서 세면 producer가 0을 읽고
				// notify를 건너뛴 직후 잠드는 창이 생긴다.
				_idleCount.fetch_add(1);

				// [재확인 범위] 자기 deque / injection / 남의 deque를 전부 본다.
				// 남의 deque까지 보는 것이 위 notify heuristic의 잔여 창을 0으로
				// 만든다. design/actor-runtime.md §5
				if (AcquireAnywhere(index, task)) {
					_idleCount.fetch_sub(1);
					lock.unlock();
					task->Execute();
					continue;
				}

				// spurious wakeup은 바깥 루프가 흡수한다
				ThreadStats().workerSleepCount += 1;
				_wakeup.wait(lock);
				_idleCount.fetch_sub(1);
			}
		}

		void WorkerPool::StopAndJoin() {
			// stopping은 sleep 락 안에서 설정해야 wait 직전의 worker도 깨운다.
			{
				std::lock_guard<std::mutex> guard(_sleepLock);
				_stopping.store(true);
			}
			_wakeup.notify_all();

			// 실행 중인 Execute가 끝날 때까지 기다린다. 중단 수단은 없다 —
			// handler가 무한정 block하면 여기서 멈춘다(설계가 감수하는 제약).
			for (std::thread& worker : _workers) {
				if (worker.joinable())
					worker.join();
			}
			_workers.clear();
		}

		void WorkerPool::DiscardPending() {
			// 남은 task를 실행하지 않고 버린다. 큐가 둘이므로 전부 비운다 —
			// local deque를 빠뜨리면 그 안의 task가 참조로 남아 정리가 밀린다.
			// worker join 이후라 아무도 이 자료구조를 만지지 않는다.
			{
				TaskPtr task;
				while (_injection.try_dequeue(task)) {
				}
			}
			for (const std::unique_ptr<LocalDeque>& deque : _local) {
				std::lock_guard<std::mutex> guard(deque->lock);
				deque->items.clear();
			}
		}

		// =======================================================================
		// RuntimeBody
		// =======================================================================

		RuntimeBody::RuntimeBody()
			: _pool(StartWorkerCount()) {
			// [필수] worker를 만들기 **전에** 계측 전역을 생성시킨다.
			//
			// worker는 계측 블록을 통해 StatsLock()/StatsRegistry()/RetiredStats()를
			// 만진다. 이 몸체가 프로세스 전역 static이므로, 계측 전역이 몸체보다
			// 나중에 만들어지면 먼저 파괴되어 아직 도는 worker가 죽은 전역을 만진다.
			//
			// 표준은 static storage duration 객체에 대해 "생성 완료의 역순으로
			// 파괴"를 보장한다([basic.start.term]). 함수나 TU 경계를 가리지 않는다.
			// 따라서 여기서 먼저 건드려 생성 순서를 stats -> body 로 고정하면
			// 파괴는 body -> stats 가 되어, join이 끝난 뒤에야 계측 전역이 사라진다.
			//
			// "worker의 thread_local 소멸자가 join 중에 도니 안전하다"는 이 자리를
			// 덮지 못한다. join을 시작하는 것이 이 객체의 소멸자이므로, 순서가
			// 반대였다면 worker는 join이 시작되기도 전에 죽은 전역을 만진다.
			//
			// 이 세 줄을 지우면 프로세스 종료 시점에만, 비결정적으로 깨진다.
			TouchStatsGlobals();

			_pool.Start();
		}

		RuntimeBody::~RuntimeBody() {
			Shutdown();
		}

		void RuntimeBody::Shutdown() {
			if (_shutdownStarted.exchange(true))
				return;

			// 순서가 계약이다. design/actor-runtime.md §8
			//
			// 1. 새 Spawn 거부. accepting 변경과 Registry 등록은 같은 락으로
			//    선형화한다. 이 락보다 먼저 등록된 Actor는 5번 snapshot에 반드시
			//    포함되고, 나중에 진입한 Spawn은 TryRegister에서 거부된다.
			_registry.CloseGate();

			// 2·3. stopping + notify 후 worker join.
			_pool.StopAndJoin();

			// 4. 남은 task discard. join 이후라 아무도 큐를 만지지 않는다.
			_pool.DiscardPending();

			// 5·6. Registry 스냅샷을 만든 뒤 락 밖에서 Stop + Finalize, 그리고 clear.
			_registry.FinalizeAll();
		}

		RuntimeBody& Instance() {
			// magic static이 기동 경합을 처리한다. 별도의 once_flag나 기동 락이
			// 필요 없고, 외부에 초기화 훅을 요구하지도 않는다.
			static RuntimeBody body;
			return body;
		}

	}

	// =======================================================================
	// 공개 API
	// =======================================================================

	void SetWorkerCount(std::size_t count) {
		// worker 0은 "Send는 성공하는데 아무도 처리하지 않는" 상태를 만든다.
		MYUTILS_ASSERT(count >= 1);

		// 기동 후의 설정은 조용히 무시되는 것이 최악이므로 그 자리에서 죽인다.
		MYUTILS_ASSERT(!GStarted.load());

		GDesiredWorkerCount.store(count);
	}

	std::size_t WorkerCount() noexcept {
		// [주의] Instance()를 부르지 않는다. 게터가 스레드를 만들면 "기동 전에
		// 확인한다"가 불가능해진다.
		return ResolveWorkerCount();
	}

	void Shutdown() {
		Detail::Instance().Shutdown();
	}
}
