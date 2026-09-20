#pragma once

// 경량 Actor Runtime.
//
//   Spawn(unique_ptr<Actor>) → ActorRef
//   ActorRef::Send(MessagePtr) → worker가 Actor::Handle을 직렬화 실행
//
// 상태 전이표, lost wakeup 프로토콜, 락 규칙, shutdown 시퀀스는
// design/actor-runtime.md 에 있다.
//
// 이 헤더를 건드리기 전에 반드시 알아야 할 것:
//   - Actor::Handle 은 mailbox 락을 보유하지 않은 상태에서 호출된다.
//     self-send, self-stop, handler 안에서의 Spawn 이 전부 여기에 의존한다.
//   - Send 가 true 여도 전달은 보장되지 않는다. accept 되었다는 뜻뿐이다.

#include <cstddef>
#include <cstdint>
#include <memory>

namespace MyUtils::Actors {

	// -----------------------------------------------------------------------
	// Message
	//
	//   Actor에게 "무엇이 일어났는가"를 나타내는 데이터. callable이 아니다.
	//   런타임이 소유하며 Handle 반환 직후 파괴된다.
	// -----------------------------------------------------------------------
	struct Message {
		Message() = default;
		virtual ~Message() = default;

		Message(const Message&) = delete;
		Message& operator=(const Message&) = delete;
	};

	using MessagePtr = std::unique_ptr<Message>;

	// -----------------------------------------------------------------------
	// Actor
	//
	//   mutable state와 message handler를 가진 객체.
	//   내부 상태는 Handle 실행 중에만 변경한다.
	// -----------------------------------------------------------------------
	class Actor {
	public:
		Actor() = default;
		virtual ~Actor() = default;

		Actor(const Actor&) = delete;
		Actor& operator=(const Actor&) = delete;

		// 런타임이 message를 소유한다. 반환 후 파괴된다.
		// 보관하거나 전달하려면 내용을 복사할 것.
		//
		// [보장] 호출 시점에 mailbox 락은 보유되어 있지 않다. 따라서 이 안에서
		//        자기 자신에게 Send / Stop 하거나 새 Actor를 Spawn해도 안전하다.
		//
		// [제약] 무한정 block하지 말 것. block하면 ActorSystem::Shutdown의
		//        worker join도 끝나지 않는다.
		//
		// 예외를 던지면 해당 Actor는 격리되어 Stop된다. handler가 중간에 던졌다면
		// Actor 내부 상태가 부분적으로만 바뀌었을 수 있어, 다음 메시지를 계속
		// 처리하지 않는다.
		virtual void Handle(Message& message) = 0;
	};

	class ActorControlBlock;   // 내부 구현
	class ActorRuntime;        // 내부 구현

	// -----------------------------------------------------------------------
	// ActorRef
	//
	//   Actor에 대한 외부 접근 핸들. 복사 가능하다.
	//
	//   [주의] 이 핸들의 수명은 Actor의 수명을 결정하지 않는다. 마지막 ActorRef가
	//   사라져도 Actor는 살아 있다. 종료시키려면 Stop() 또는 시스템 Shutdown이
	//   필요하다.
	// -----------------------------------------------------------------------
	class ActorRef {
	public:
		ActorRef() noexcept = default;

		bool IsValid() const noexcept { return _cb != nullptr; }
		explicit operator bool() const noexcept { return IsValid(); }

		// true  = 호출 시점에 Actor가 message를 accept했다.
		//         Handle까지 실행된다는 보장은 아니다 — 이후 Stop/Shutdown이
		//         accept된 메시지를 버릴 수 있다.
		// false = Actor가 STOPPING/DEAD 이거나 시스템이 종료 상태다.
		//         둘을 구분할 방법은 없다.
		//
		// [주의] 현재 mailbox가 mutex + deque라서 producer 사이의 순서까지 전역
		// FIFO로 확정된다. 이건 구현의 부수 효과이므로 프로토콜이 전역 순서에
		// 의존하게 만들지 말 것 — mailbox를 lock-free로 바꾸면 사라진다.
		bool Send(MessagePtr message) const;

		// 즉시 중단한다. graceful stop이 아니다.
		// 실행 중인 handler만 완료되고 pending 메시지는 버려진다.
		//
		// "마지막 저장 메시지를 보내고 Stop"은 조용히 실패한다. graceful이
		// 필요하면 StopAfterDrain()을 별도 API로 추가할 것 — Stop에 drain 의미를
		// 섞으면 자기 자신에게 계속 Send하는 액터가 영원히 종료되지 않는다.
		void Stop() const;

	private:
		friend class ActorSystem;
		explicit ActorRef(std::shared_ptr<ActorControlBlock> cb) noexcept;

		std::shared_ptr<ActorControlBlock> _cb;
	};

	// -----------------------------------------------------------------------
	// 정책 상수
	// -----------------------------------------------------------------------

	// 한 번 스케줄될 때 처리할 최대 메시지 수. 소진 후 메일박스가 비지 않았으면
	// 다시 runnable로 등록된다. 한 액터가 worker를 독점하지 않게 끊는 장치다.
	// 측정에서 나온 값이 아니라 baseline이다.
	inline constexpr std::size_t DEFAULT_MESSAGE_BUDGET = 32;

	// worker가 몇 번에 한 번 injection queue를 local deque보다 먼저 볼 것인가.
	//
	// 이게 없으면 local deque가 계속 차 있는 동안 injection을 한 번도 보지 않아
	// 외부 스레드·I/O completion의 runnable이 무한정 밀린다. 최적값이 아니라
	// baseline이다.
	//
	// [주의] DEFAULT_MESSAGE_BUDGET과 상호작용한다. 한 번의 local 처리가 최대
	// 32건이므로 injection 확인 간격은 최악의 경우 메시지 1,952건이다.
	// 둘 중 하나를 바꾸면 다른 쪽의 의미도 바뀐다.
	inline constexpr std::size_t INJECTION_POLL_INTERVAL = 61;

	// -----------------------------------------------------------------------
	// ActorSystem
	//
	//   worker thread pool과 Actor Registry를 소유한다.
	//   Registry가 Actor의 논리적 수명을 쥔다.
	// -----------------------------------------------------------------------
	class ActorSystem {
	public:
		// workerCount는 1 이상이어야 한다. 0이면 Send는 성공하는데 아무도
		// 처리하지 않는 상태가 되므로 계약 위반으로 막는다.
		explicit ActorSystem(std::size_t workerCount);
		~ActorSystem();

		ActorSystem(const ActorSystem&) = delete;
		ActorSystem& operator=(const ActorSystem&) = delete;

		// unique_ptr로 받는 것이 중요하다. shared_ptr이면 호출자가 Actor state에
		// 직접 접근할 수 있어 isolation invariant가 API 수준에서 뚫린다.
		//
		// 시스템이 종료 중이면 무효 ActorRef를 돌려준다.
		[[nodiscard]] ActorRef Spawn(std::unique_ptr<Actor> actor);

		// 여러 번 불러도 안전하다. 소멸자가 자동으로 호출한다.
		void Shutdown();

		std::size_t WorkerCount() const noexcept;

		// 진단용. 아직 Finalize되지 않은 Actor 수.
		std::size_t LiveActorCount() const;

	private:
		std::shared_ptr<ActorRuntime> _runtime;
	};

	// -----------------------------------------------------------------------
	// 계측
	//
	//   hot path는 자기 thread의 블록만 만지므로 동기화가 없다.
	//   블록의 생성자/소멸자가 등록과 집계를 처리하므로, thread가 죽어도
	//   누적값을 잃지 않고 별도 정리 훅도 필요 없다.
	// -----------------------------------------------------------------------
	struct ActorStats {
		// --- Send ---
		std::uint64_t sendAccepted = 0;
		std::uint64_t sendRejected = 0;                 // Actor가 STOPPING/DEAD

		// --- 스케줄링 ---
		std::uint64_t scheduleCount = 0;                // IDLE -> SCHEDULED 성공
		std::uint64_t scheduleAbortedSystemStopping = 0;// 시스템 종료 중이라 등록 실패
		std::uint64_t runCount = 0;                     // 배치 실행 횟수
		std::uint64_t messagesHandled = 0;

		// runnable 등록부터 worker가 집기까지 걸린 시간의 합.
		// queueWaitUsTotal / runCount 가 평균이다. 배치당 clock 1회.
		std::uint64_t queueWaitUsTotal = 0;

		// --- 수명 ---
		std::uint64_t spawnCount = 0;
		std::uint64_t finalizeCount = 0;
		std::uint64_t handlerExceptionCount = 0;

		// --- worker ---
		std::uint64_t workerSleepCount = 0;             // cv.wait 진입 횟수

		// --- 스케줄러 라우팅 ---
		//
		// localPush / injectionPush 비율이 locality 라우팅이 실제로 도는지 보여준다.
		// injectionPush만 늘면 worker가 만든 runnable이 local로 안 가고 있다는 뜻이며,
		// 라우팅 테스트들이 이 두 값으로 경계를 확인한다.
		std::uint64_t localPushCount = 0;
		std::uint64_t injectionPushCount = 0;
	};

	// 모든 thread의 값을 합산한 스냅샷. 이미 종료된 thread의 누적분도 포함한다.
	//
	// [주의] 살아 있는 thread의 카운터를 동기화 없이 읽으므로 엄밀히는 data race다.
	// 통계 목적이라 허용한다. 정확한 값이 필요하면 대상 thread를 join한 뒤 읽을 것.
	ActorStats SnapshotStats();
}
