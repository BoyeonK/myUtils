#include "MyUtils/Actor.h"
#include "MyUtils/Assert.h"
#include "MyUtils/Profiling.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "concurrentqueue.h"

// 구현 전체가 design/actor-runtime.md 의 규약을 따른다.
// 특히 아래 세 가지는 어기면 조용히 깨지므로 각 지점에 주석을 달아뒀다.
//   L1: Handle 은 mailbox 락 밖에서 호출한다
//   L2: registry 락과 mailbox 락을 중첩하지 않는다
//   L4: 메시지 파괴는 mailbox 락 밖에서 한다

namespace MyUtils::Actors {

	enum class ActorState : std::uint8_t {
		IDLE,
		SCHEDULED,
		RUNNING,
		STOPPING,
		DEAD,
	};

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

		std::vector<ActorStats*>& StatsRegistry() {
			static std::vector<ActorStats*> registry;
			return registry;
		}

		ActorStats& RetiredStats() {
			static ActorStats retired;
			return retired;
		}

		void AccumulateInto(ActorStats& dst, const ActorStats& src) {
			dst.sendAccepted += src.sendAccepted;
			dst.sendRejected += src.sendRejected;
			dst.scheduleCount += src.scheduleCount;
			dst.scheduleAbortedSystemStopping += src.scheduleAbortedSystemStopping;
			dst.runCount += src.runCount;
			dst.messagesHandled += src.messagesHandled;
			dst.budgetExhaustedCount += src.budgetExhaustedCount;
			dst.queueWaitUsTotal += src.queueWaitUsTotal;
			dst.spawnCount += src.spawnCount;
			dst.finalizeCount += src.finalizeCount;
			dst.handlerExceptionCount += src.handlerExceptionCount;
			dst.workerSleepCount += src.workerSleepCount;
			dst.mailboxLockCount += src.mailboxLockCount;
			dst.mailboxWaitUsTotal += src.mailboxWaitUsTotal;
		}

		class ThreadStatsBlock {
		public:
			ThreadStatsBlock() {
				std::lock_guard<std::mutex> guard(StatsLock());
				StatsRegistry().push_back(&_values);
			}

			~ThreadStatsBlock() {
				std::lock_guard<std::mutex> guard(StatsLock());
				std::vector<ActorStats*>& registry = StatsRegistry();
				registry.erase(
					std::remove(registry.begin(), registry.end(), &_values),
					registry.end());
				AccumulateInto(RetiredStats(), _values);
			}

			ThreadStatsBlock(const ThreadStatsBlock&) = delete;
			ThreadStatsBlock& operator=(const ThreadStatsBlock&) = delete;

			ActorStats& Values() noexcept { return _values; }

		private:
			ActorStats _values;
		};

		ActorStats& Stats() noexcept {
			thread_local ThreadStatsBlock block;
			return block.Values();
		}

		std::uint64_t NowUs() noexcept {
			using namespace std::chrono;
			return static_cast<std::uint64_t>(
				duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
		}

		// 계측 3단 프로브를 단 mailbox 락.
		//
		// 플래그가 꺼져 있으면 clock을 읽지 않으므로 평범한 lock_guard와 같다.
		// 꺼졌을 때 비용은 relaxed load 한 번과 예측되는 분기뿐이다. ADR-0013
		class TimedMailboxLock {
		public:
			explicit TimedMailboxLock(std::mutex& mailboxLock) : _lock(&mailboxLock) {
				if (!IsProfilingEnabled()) {
					_lock->lock();
					return;
				}

				const std::uint64_t begin = NowUs();
				_lock->lock();

				ActorStats& stats = Stats();
				stats.mailboxLockCount += 1;
				stats.mailboxWaitUsTotal += NowUs() - begin;
			}

			~TimedMailboxLock() { _lock->unlock(); }

			TimedMailboxLock(const TimedMailboxLock&) = delete;
			TimedMailboxLock& operator=(const TimedMailboxLock&) = delete;

		private:
			std::mutex* _lock = nullptr;
		};
	}

	ActorStats SnapshotStats() {
		std::lock_guard<std::mutex> guard(StatsLock());

		ActorStats total = RetiredStats();
		for (const ActorStats* block : StatsRegistry())
			AccumulateInto(total, *block);

		return total;
	}

	// =======================================================================
	// ActorControlBlock
	// =======================================================================
	class ActorControlBlock : public std::enable_shared_from_this<ActorControlBlock> {
	public:
		ActorControlBlock(std::unique_ptr<Actor> actor, std::weak_ptr<ActorRuntime> runtime) noexcept
			: _runtime(std::move(runtime))
			, _actor(std::move(actor)) {
		}

		ActorControlBlock(const ActorControlBlock&) = delete;
		ActorControlBlock& operator=(const ActorControlBlock&) = delete;

		bool Send(MessagePtr message);
		void Stop();
		void Run(std::size_t budget);
		void Finalize();

	private:
		void TrySchedule();
		bool MailboxEmpty();

		std::weak_ptr<ActorRuntime> _runtime;

		// mailbox 락으로 보호하지 않는다. Finalize가 유일한 writer이고,
		// Finalize는 state가 RUNNING인 동안 실행될 수 없으므로 Run과 겹치지 않는다.
		// (Stop은 CAS 루프로 prev를 잡아 RUNNING이면 worker에게 Finalize를 넘긴다)
		std::unique_ptr<Actor> _actor;

		std::mutex _mailboxLock;
		std::deque<MessagePtr> _mailbox;

		std::atomic<ActorState> _state{ ActorState::IDLE };
		std::atomic<bool> _finalizeStarted{ false };

		// 계측 2단. runnable 등록 시각. 큐 대기 시간을 재는 데 쓴다.
		// 등록한 thread가 쓰고 worker가 읽는다. 순서는 큐가 보장한다.
		std::atomic<std::uint64_t> _enqueuedAtUs{ 0 };
	};

	// =======================================================================
	// ActorRuntime — Scheduler + Registry
	//
	//   설계 문서는 Scheduler와 Registry를 논리적으로 나누지만, ACB가 둘 다
	//   weak로 참조해야 해서 하나의 내부 객체로 묶었다. 소유 방향(ACB -> weak)은
	//   그대로다.
	// =======================================================================
	class ActorRuntime : public std::enable_shared_from_this<ActorRuntime> {
	public:
		explicit ActorRuntime(std::size_t workerCount) noexcept
			: _workerCount(workerCount) {
		}

		~ActorRuntime() { Shutdown(); }

		ActorRuntime(const ActorRuntime&) = delete;
		ActorRuntime& operator=(const ActorRuntime&) = delete;

		void Start();
		void Shutdown();

		// runnable을 만드는 유일한 경로다. 큐 삽입과 notify가 여기서 원자적으로
		// 묶인다. 우회하면 그 경로에서만 worker가 깨지 않는다. ADR-0011
		bool EnqueueRunnable(std::shared_ptr<ActorControlBlock> cb);

		void Register(std::shared_ptr<ActorControlBlock> cb);
		void Unregister(const std::shared_ptr<ActorControlBlock>& cb);

		bool IsAccepting() const noexcept { return _accepting.load(); }
		std::size_t WorkerCount() const noexcept { return _workerCount; }
		std::size_t LiveActorCount() const;

	private:
		void WorkerLoop();

		moodycamel::ConcurrentQueue<std::shared_ptr<ActorControlBlock>> _runnable;

		std::mutex _sleepLock;
		std::condition_variable _wakeup;
		std::atomic<bool> _stopping{ false };
		std::atomic<bool> _accepting{ true };
		std::atomic<bool> _shutdownStarted{ false };

		mutable std::mutex _registryLock;
		std::unordered_set<std::shared_ptr<ActorControlBlock>> _registry;

		std::size_t _workerCount = 0;
		std::vector<std::thread> _workers;
	};

	// =======================================================================
	// ActorControlBlock 구현
	// =======================================================================

	bool ActorControlBlock::Send(MessagePtr message) {
		if (message == nullptr)
			return false;

		bool accepted = false;
		{
			// acceptance gate. Stop도 같은 락 안에서 STOPPING으로 전이하므로
			// "Stop 이후 accept" 여부가 락 획득 순서 하나로 결정된다.
			TimedMailboxLock guard(_mailboxLock);

			const ActorState state = _state.load();
			if (state != ActorState::STOPPING && state != ActorState::DEAD) {
				_mailbox.push_back(std::move(message));
				accepted = true;
			}
		}

		ActorStats& stats = Stats();
		if (accepted)
			stats.sendAccepted += 1;
		else
			stats.sendRejected += 1;

		// 거부되었다면 여기서 파괴된다. 락 밖이어야 한다 (L4) —
		// 메시지가 ActorRef를 들고 있고 소멸자가 다시 Send할 수 있다.
		message.reset();

		if (accepted)
			TrySchedule();

		return accepted;
	}

	void ActorControlBlock::Stop() {
		ActorState prev = _state.load();
		{
			TimedMailboxLock guard(_mailboxLock);

			// [주의] prev를 load 후 store하면 안 된다. IDLE -> SCHEDULED 전이는
			// 이 락 밖에서 일어나므로, 그 사이에 producer가 SCHEDULED로 바꾸고
			// 큐에 등록할 수 있다. 그러면 prev를 IDLE로 잘못 믿고 여기서
			// Finalize하게 된다. CAS 루프로 원자적으로 포착해야 한다.
			for (;;) {
				if (prev == ActorState::STOPPING || prev == ActorState::DEAD)
					return;
				if (_state.compare_exchange_weak(prev, ActorState::STOPPING))
					break;
				// 실패 시 prev가 현재 값으로 갱신된다
			}
		}

		// prev == IDLE                : 아무도 실행 중이 아니다. 여기서 정리
		// prev == SCHEDULED / RUNNING : worker가 전이 CAS 실패로 감지해 정리
		if (prev == ActorState::IDLE)
			Finalize();
	}

	void ActorControlBlock::Run(std::size_t budget) {
		ActorState expected = ActorState::SCHEDULED;
		if (!_state.compare_exchange_strong(expected, ActorState::RUNNING)) {
			// SCHEDULED가 아니다 = Stop이 들어왔거나 이미 DEAD다.
			// once gate가 중복 Finalize를 막는다.
			Finalize();
			return;
		}

		// 계측 2단. 배치당 clock 1회.
		ActorStats& stats = Stats();
		stats.runCount += 1;
		const std::uint64_t enqueuedAtUs = _enqueuedAtUs.load(std::memory_order_relaxed);
		if (enqueuedAtUs != 0)
			stats.queueWaitUsTotal += NowUs() - enqueuedAtUs;

		// state가 RUNNING인 동안에는 Finalize가 실행될 수 없으므로 안전하다.
		Actor* actor = _actor.get();
		ASSERT_CRASH(actor != nullptr);

		std::size_t processed = 0;
		for (; processed < budget; ++processed) {
			MessagePtr message;
			{
				TimedMailboxLock guard(_mailboxLock);
				if (_mailbox.empty())
					break;
				message = std::move(_mailbox.front());
				_mailbox.pop_front();
			}

			// [L1] 락을 놓은 뒤에 Handle을 부른다.
			// 락을 쥔 채 부르면 self-send / self-stop / 메시지 소멸자의 Send가
			// 전부 데드락한다. "배치니까 락을 한 번만 잡자"는 최적화 금지.
			try {
				actor->Handle(*message);
			}
			catch (...) {
				// handler가 던졌다면 Actor state가 부분적으로만 바뀌었을 수 있다.
				// 다음 메시지를 계속 처리하지 않고 격리한다. ADR-0012
				stats.handlerExceptionCount += 1;
				message.reset();   // 락 밖 파괴 (L4)
				Stop();
				break;             // drain 루프 즉시 탈출
			}

			stats.messagesHandled += 1;

			// message는 여기서 락 밖에서 파괴된다 (L4)
		}

		// budget을 다 썼다면 DEFAULT_MESSAGE_BUDGET이 작다는 신호일 수 있다
		if (processed == budget)
			stats.budgetExhaustedCount += 1;

		ActorState running = ActorState::RUNNING;
		if (!_state.compare_exchange_strong(running, ActorState::IDLE)) {
			// 실행 중에 Stop이 들어왔다.
			Finalize();
			return;
		}

		// lost wakeup 방지 재확인. 반드시 RUNNING -> IDLE 전이 "이후"여야 한다.
		// design/actor-runtime.md §4
		if (!MailboxEmpty())
			TrySchedule();
	}

	void ActorControlBlock::Finalize() {
		if (_finalizeStarted.exchange(true))
			return;   // 이미 누군가 수행 중이거나 완료했다

		Stats().finalizeCount += 1;

		// Unregister가 registry의 마지막 참조를 놓을 수 있다. 그 자리에서
		// 객체가 파괴되면 아래 state.store가 dangling이 되므로 붙잡아둔다.
		const std::shared_ptr<ActorControlBlock> self = shared_from_this();

		std::deque<MessagePtr> drained;
		{
			TimedMailboxLock guard(_mailboxLock);
			drained.swap(_mailbox);   // 락 안에서는 swap만
		}

		drained.clear();   // 락 밖 파괴 (L4)
		_actor.reset();    // 락 밖. Actor 소멸자가 Send할 수 있다

		// [L2] mailbox 락을 놓은 뒤에 registry를 만진다
		if (const std::shared_ptr<ActorRuntime> runtime = _runtime.lock())
			runtime->Unregister(self);

		// cleanup 완료 후에 DEAD를 쓴다. STOPPING 구간에서도 Send는 거부되므로
		// 틈이 생기지 않는다.
		_state.store(ActorState::DEAD);
	}

	void ActorControlBlock::TrySchedule() {
		ActorState expected = ActorState::IDLE;
		if (!_state.compare_exchange_strong(expected, ActorState::SCHEDULED))
			return;   // 이미 SCHEDULED/RUNNING 이거나 종료 중이다

		// CAS 성공자만 등록한다. 큐에 같은 ACB가 두 번 들어갈 경로가 없다.
		_enqueuedAtUs.store(NowUs(), std::memory_order_relaxed);

		const std::shared_ptr<ActorRuntime> runtime = _runtime.lock();
		if (runtime && runtime->EnqueueRunnable(shared_from_this())) {
			Stats().scheduleCount += 1;
			return;
		}

		// 시스템이 종료 중이라 등록하지 못했다. 되돌린다.
		Stats().scheduleAbortedSystemStopping += 1;
		ActorState scheduled = ActorState::SCHEDULED;
		_state.compare_exchange_strong(scheduled, ActorState::IDLE);
	}

	bool ActorControlBlock::MailboxEmpty() {
		TimedMailboxLock guard(_mailboxLock);
		return _mailbox.empty();
	}

	// =======================================================================
	// ActorRuntime 구현
	// =======================================================================

	void ActorRuntime::Start() {
		_workers.reserve(_workerCount);
		for (std::size_t i = 0; i < _workerCount; ++i)
			_workers.emplace_back([this] { WorkerLoop(); });
	}

	bool ActorRuntime::EnqueueRunnable(std::shared_ptr<ActorControlBlock> cb) {
		if (_stopping.load())
			return false;

		if (!_runnable.enqueue(std::move(cb)))
			return false;

		// 빈 임계구역이 장치다. worker가 "큐 재확인 -> wait 진입" 구간에 있으면
		// 여기서 락을 얻지 못해 대기하고, worker가 wait에 들어가 락을 놓은 뒤에야
		// notify가 나간다. worker가 아직 구간에 들어오지 않았다면 재확인 시점에
		// enqueue가 이미 끝나 있어 큐에서 발견한다. design/actor-runtime.md §5
		{
			std::lock_guard<std::mutex> guard(_sleepLock);
		}
		_wakeup.notify_one();
		return true;
	}

	void ActorRuntime::WorkerLoop() {
		for (;;) {
			if (_stopping.load())
				return;

			std::shared_ptr<ActorControlBlock> cb;
			if (_runnable.try_dequeue(cb)) {
				cb->Run(DEFAULT_MESSAGE_BUDGET);
				continue;
			}

			std::unique_lock<std::mutex> lock(_sleepLock);
			if (_stopping.load())
				return;
			if (_runnable.try_dequeue(cb)) {
				lock.unlock();
				cb->Run(DEFAULT_MESSAGE_BUDGET);
				continue;
			}

			// spurious wakeup은 바깥 루프가 흡수한다
			Stats().workerSleepCount += 1;
			_wakeup.wait(lock);
		}
	}

	void ActorRuntime::Register(std::shared_ptr<ActorControlBlock> cb) {
		std::lock_guard<std::mutex> guard(_registryLock);
		_registry.insert(std::move(cb));
	}

	void ActorRuntime::Unregister(const std::shared_ptr<ActorControlBlock>& cb) {
		std::lock_guard<std::mutex> guard(_registryLock);
		_registry.erase(cb);
	}

	std::size_t ActorRuntime::LiveActorCount() const {
		std::lock_guard<std::mutex> guard(_registryLock);
		return _registry.size();
	}

	void ActorRuntime::Shutdown() {
		if (_shutdownStarted.exchange(true))
			return;

		// 1. 새 Spawn 거부
		_accepting.store(false);

		// 2. stopping + notify. sleep 락 안에서 설정해야 wait 직전의 worker도 깨운다.
		{
			std::lock_guard<std::mutex> guard(_sleepLock);
			_stopping.store(true);
		}
		_wakeup.notify_all();

		// 3. worker join — 실행 중인 Handler가 끝날 때까지 기다린다.
		//    Handler가 무한정 block하면 여기서 멈춘다(설계가 감수하는 제약).
		for (std::thread& worker : _workers) {
			if (worker.joinable())
				worker.join();
		}
		_workers.clear();

		// 4. 남은 runnable discard
		{
			std::shared_ptr<ActorControlBlock> cb;
			while (_runnable.try_dequeue(cb)) {
			}
		}

		// 5. Registry 스냅샷을 만든 뒤 락 밖에서 Finalize.
		//    Finalize가 Unregister를 하므로 순회 중에 부르면 뮤텍스 재획득으로
		//    데드락하거나 iterator가 무효화된다. design/actor-runtime.md §8
		std::vector<std::shared_ptr<ActorControlBlock>> snapshot;
		{
			std::lock_guard<std::mutex> guard(_registryLock);
			snapshot.assign(_registry.begin(), _registry.end());
		}

		for (const std::shared_ptr<ActorControlBlock>& cb : snapshot) {
			cb->Stop();       // STOPPING 전이. prev에 따라 Stop이 Finalize할 수도 있다
			cb->Finalize();   // worker가 없으므로 여기서 확정적으로 끝낸다(once gate)
		}
		snapshot.clear();

		// 6. Registry clear
		{
			std::lock_guard<std::mutex> guard(_registryLock);
			_registry.clear();
		}
	}

	// =======================================================================
	// ActorRef
	// =======================================================================

	ActorRef::ActorRef(std::shared_ptr<ActorControlBlock> cb) noexcept
		: _cb(std::move(cb)) {
	}

	bool ActorRef::Send(MessagePtr message) const {
		if (_cb == nullptr)
			return false;
		return _cb->Send(std::move(message));
	}

	void ActorRef::Stop() const {
		if (_cb != nullptr)
			_cb->Stop();
	}

	// =======================================================================
	// ActorSystem
	// =======================================================================

	ActorSystem::ActorSystem(std::size_t workerCount) {
		// workerCount == 0 이면 Send는 성공하는데 아무도 처리하지 않는 상태가 된다
		ASSERT_CRASH(workerCount >= 1);

		_runtime = std::make_shared<ActorRuntime>(workerCount);
		_runtime->Start();
	}

	ActorSystem::~ActorSystem() {
		if (_runtime)
			_runtime->Shutdown();
	}

	ActorRef ActorSystem::Spawn(std::unique_ptr<Actor> actor) {
		ASSERT_CRASH(actor != nullptr);

		if (!_runtime->IsAccepting())
			return ActorRef{};

		std::shared_ptr<ActorControlBlock> cb =
			std::make_shared<ActorControlBlock>(std::move(actor), _runtime);

		_runtime->Register(cb);
		Stats().spawnCount += 1;
		return ActorRef{ std::move(cb) };
	}

	void ActorSystem::Shutdown() {
		_runtime->Shutdown();
	}

	std::size_t ActorSystem::WorkerCount() const noexcept {
		return _runtime->WorkerCount();
	}

	std::size_t ActorSystem::LiveActorCount() const {
		return _runtime->LiveActorCount();
	}
}
