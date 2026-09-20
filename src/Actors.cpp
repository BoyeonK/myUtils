#include "MyUtils/Actor.h"
#include "MyUtils/Assert.h"

#include "RuntimeInternal.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

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
	// ActorControlBlock
	//
	//   pool이 실행하는 Task의 한 종류다. pool은 이 타입을 알지 못하고
	//   Execute()만 부른다. 배치 크기(message budget)는 Actor의 정책이므로
	//   여기서 정한다.
	// =======================================================================
	class ActorControlBlock
		: public Runtime::Detail::Task
		, public std::enable_shared_from_this<ActorControlBlock> {
	public:
		explicit ActorControlBlock(std::unique_ptr<Actor> actor) noexcept
			: _actor(std::move(actor)) {
		}

		ActorControlBlock(const ActorControlBlock&) = delete;
		ActorControlBlock& operator=(const ActorControlBlock&) = delete;

		bool Send(MessagePtr message);
		void Stop();
		void Finalize();

		void Execute() override;

	private:
		void TrySchedule();
		bool MailboxEmpty();

		// mailbox 락으로 보호하지 않는다. Finalize가 유일한 writer이고,
		// Finalize는 state가 RUNNING인 동안 실행될 수 없으므로 Execute와 겹치지 않는다.
		// (Stop은 CAS 루프로 prev를 잡아 RUNNING이면 worker에게 Finalize를 넘긴다)
		std::unique_ptr<Actor> _actor;

		std::mutex _mailboxLock;
		std::deque<MessagePtr> _mailbox;

		std::atomic<ActorState> _state{ ActorState::IDLE };
		std::atomic<bool> _finalizeStarted{ false };
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
			//
			// [LT1] 이 검사는 런타임 몸체 접근보다 **반드시 앞선다.** 몸체는
			// 프로세스 전역 static이라 ActorRef보다 먼저 파괴될 수 있는데,
			// 종료가 모든 ACB를 DEAD로 만들어두므로 stale 핸들의 Send는 여기서
			// 돌아선다. 검사를 아래로 옮기면 그 순간 UAF가 열린다.
			std::lock_guard<std::mutex> guard(_mailboxLock);

			const ActorState state = _state.load();
			if (state != ActorState::STOPPING && state != ActorState::DEAD) {
				_mailbox.push_back(std::move(message));
				accepted = true;
			}
		}

		Runtime::Stats& stats = Runtime::Detail::ThreadStats();
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
			std::lock_guard<std::mutex> guard(_mailboxLock);

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

	void ActorControlBlock::Execute() {
		ActorState expected = ActorState::SCHEDULED;
		if (!_state.compare_exchange_strong(expected, ActorState::RUNNING)) {
			// SCHEDULED가 아니다 = Stop이 들어왔거나 이미 DEAD다.
			// once gate가 중복 Finalize를 막는다.
			Finalize();
			return;
		}

		// 계측. 배치당 clock 1회라 메시지당으로 환산하면 묻힌다.
		Runtime::Stats& stats = Runtime::Detail::ThreadStats();
		stats.runCount += 1;
		const std::uint64_t submittedAtUs = enqueuedAtUs.load(std::memory_order_relaxed);
		if (submittedAtUs != 0)
			stats.queueWaitUsTotal += Runtime::Detail::NowUs() - submittedAtUs;

		// state가 RUNNING인 동안에는 Finalize가 실행될 수 없으므로 안전하다.
		Actor* actor = _actor.get();
		MYUTILS_ASSERT(actor != nullptr);

		std::size_t processed = 0;
		for (; processed < DEFAULT_MESSAGE_BUDGET; ++processed) {
			MessagePtr message;
			{
				std::lock_guard<std::mutex> guard(_mailboxLock);

				// [consumption boundary] Stop은 이 락을 쥔 채 STOPPING을 쓴다.
				// 따라서 락을 통과한 Stop은 반드시 여기서 보이고, 그 이후로는
				// 어떤 메시지도 새로 pop되지 않는다. Send의 acceptance boundary와
				// 같은 락으로 대칭을 이룬다. design/actor-runtime.md §6
				//
				// 이 검사가 없으면 Stop은 state만 바꿀 뿐 worker는 여전히 이 루프
				// 안이므로, pending 메시지를 budget 한도까지 계속 처리한다 —
				// "Stop은 pending을 버린다"는 계약과 정면으로 어긋난다.
				if (_state.load() != ActorState::RUNNING)
					break;

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
				// 다음 메시지를 계속 처리하지 않고 격리한다.
				stats.handlerExceptionCount += 1;
				message.reset();   // 락 밖 파괴 (L4)
				Stop();
				break;             // drain 루프 즉시 탈출
			}

			stats.messagesHandled += 1;

			// message는 여기서 락 밖에서 파괴된다 (L4)
		}

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

		Runtime::Detail::ThreadStats().finalizeCount += 1;

		// Unregister가 registry의 마지막 참조를 놓을 수 있다. 그 자리에서
		// 객체가 파괴되면 아래 state.store가 dangling이 되므로 붙잡아둔다.
		const std::shared_ptr<ActorControlBlock> self = shared_from_this();

		std::deque<MessagePtr> drained;
		{
			std::lock_guard<std::mutex> guard(_mailboxLock);
			drained.swap(_mailbox);   // 락 안에서는 swap만
		}

		drained.clear();   // 락 밖 파괴 (L4)
		_actor.reset();    // 락 밖. Actor 소멸자가 Send할 수 있다

		// [L2] mailbox 락을 놓은 뒤에 registry를 만진다.
		//
		// 몸체 접근이 여기 있어도 안전한 이유: once gate를 통과하는 경로는
		// Stop / Execute / 종료 5단계뿐이고, 셋 다 몸체가 살아 있는 동안에만
		// 도달한다. ActorRef가 마지막 참조를 놓아 ACB가 파괴되는 경로는
		// Finalize를 부르지 않는다.
		Runtime::Detail::Instance().Registry().Unregister(self);

		// cleanup 완료 후에 DEAD를 쓴다. STOPPING 구간에서도 Send는 거부되므로
		// 틈이 생기지 않는다.
		_state.store(ActorState::DEAD);
	}

	void ActorControlBlock::TrySchedule() {
		ActorState expected = ActorState::IDLE;
		if (!_state.compare_exchange_strong(expected, ActorState::SCHEDULED))
			return;   // 이미 SCHEDULED/RUNNING 이거나 종료 중이다

		// CAS 성공자만 등록한다. 큐에 같은 ACB가 두 번 들어갈 경로가 없다.
		Runtime::Stats& stats = Runtime::Detail::ThreadStats();
		if (Runtime::Detail::Instance().Pool().Submit(shared_from_this())) {
			stats.scheduleCount += 1;
			return;
		}

		// 런타임이 종료 중이라 등록하지 못했다. 되돌린다.
		stats.scheduleAbortedSystemStopping += 1;
		ActorState scheduled = ActorState::SCHEDULED;
		_state.compare_exchange_strong(scheduled, ActorState::IDLE);
	}

	bool ActorControlBlock::MailboxEmpty() {
		std::lock_guard<std::mutex> guard(_mailboxLock);
		return _mailbox.empty();
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
	// 진입점
	// =======================================================================

	ActorRef Spawn(std::unique_ptr<Actor> actor) {
		MYUTILS_ASSERT(actor != nullptr);

		// 첫 호출이 런타임을 기동시킨다.
		Runtime::Detail::RuntimeBody& body = Runtime::Detail::Instance();

		if (!body.Registry().IsAccepting())
			return ActorRef{};

		std::shared_ptr<ActorControlBlock> cb =
			std::make_shared<ActorControlBlock>(std::move(actor));

		// 위 IsAccepting은 종료 후 불필요한 할당을 피하는 fast path일 뿐이다.
		// 종료와의 실제 선형화 지점은 Registry 락 아래의 TryRegister다.
		if (!body.Registry().TryRegister(cb))
			return ActorRef{};

		Runtime::Detail::ThreadStats().spawnCount += 1;
		return ActorRef{ std::move(cb) };
	}

	std::size_t LiveActorCount() {
		return Runtime::Detail::Instance().Registry().LiveActorCount();
	}
}

namespace MyUtils::Runtime::Detail {

	// =======================================================================
	// ActorRegistry
	//
	//   ActorControlBlock의 완전한 타입이 필요하므로 정의가 여기에 있다.
	// =======================================================================

	bool ActorRegistry::TryRegister(std::shared_ptr<Actors::ActorControlBlock> cb) {
		std::lock_guard<std::mutex> guard(_lock);
		if (!_accepting.load())
			return false;
		_registry.insert(std::move(cb));
		return true;
	}

	void ActorRegistry::Unregister(const std::shared_ptr<Actors::ActorControlBlock>& cb) {
		std::lock_guard<std::mutex> guard(_lock);
		_registry.erase(cb);
	}

	std::size_t ActorRegistry::LiveActorCount() const {
		std::lock_guard<std::mutex> guard(_lock);
		return _registry.size();
	}

	void ActorRegistry::CloseGate() {
		// accepting 변경과 Registry 등록은 같은 락으로 선형화한다. 이 락보다
		// 먼저 등록된 Actor는 FinalizeAll의 snapshot에 반드시 포함되고,
		// 나중에 진입한 Spawn은 TryRegister에서 거부된다.
		std::lock_guard<std::mutex> guard(_lock);
		_accepting.store(false);
	}

	void ActorRegistry::FinalizeAll() {
		// [필수] 스냅샷을 먼저 뜬다. Finalize가 Unregister를 하므로 순회 중에
		// 부르면 뮤텍스 재획득으로 데드락하거나 iterator가 무효화된다.
		// design/actor-runtime.md §8
		std::vector<std::shared_ptr<Actors::ActorControlBlock>> snapshot;
		{
			std::lock_guard<std::mutex> guard(_lock);
			snapshot.assign(_registry.begin(), _registry.end());
		}

		// Stop()을 먼저 부르는 것이 Finalize()만 부르는 것보다 강하다. Finalize는
		// state를 바로 DEAD로 쓰지만 그 전까지는 Send가 accept될 수 있고, Stop은
		// mailbox 락 안에서 STOPPING으로 전이해 그 창을 닫는다.
		//
		// [LT2] 이 루프가 "종료 후 살아남은 ACB는 전부 DEAD"를 만든다. Send 쪽의
		// LT1이 그 위에 선다. 이 단계를 약화하면 stale ActorRef의 안전성이 깨진다.
		for (const std::shared_ptr<Actors::ActorControlBlock>& cb : snapshot) {
			cb->Stop();       // STOPPING 전이. prev에 따라 Stop이 Finalize할 수도 있다
			cb->Finalize();   // worker가 없으므로 여기서 확정적으로 끝낸다(once gate)
		}
		snapshot.clear();

		{
			std::lock_guard<std::mutex> guard(_lock);
			_registry.clear();
		}
	}
}
