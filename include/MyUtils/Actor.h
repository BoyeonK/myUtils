#pragma once

// 경량 Actor Runtime.
//
//   Spawn(unique_ptr<Actor>) → ActorRef
//   ActorRef::Send(MessagePtr) → worker가 Actor::Handle을 직렬화 실행
//
// Actor는 프로세스 전역 런타임(MyUtils/Runtime.h) 위에서 돈다. worker 수 설정과
// 종료는 그쪽에 있다. 여기에는 Actor 고유의 것만 있다.
//
// 상태 전이표, lost wakeup 프로토콜, 락 규칙, shutdown 시퀀스는
// design/actor-runtime.md 에 있다.
//
// 이 헤더를 건드리기 전에 반드시 알아야 할 것:
//   - Actor::Handle 은 mailbox 락을 보유하지 않은 상태에서 호출된다.
//     self-send, self-stop, handler 안에서의 Spawn 이 전부 여기에 의존한다.
//   - Send 가 true 여도 전달은 보장되지 않는다. accept 되었다는 뜻뿐이다.
//   - Send 의 STOPPING/DEAD 검사는 런타임 몸체 접근보다 반드시 앞선다.
//     이 순서가 종료 이후 stale ActorRef 의 안전성을 만든다.

#include <cstddef>
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
		// [제약] 무한정 block하지 말 것. block하면 Runtime::Shutdown의 worker
		//        join도 끝나지 않는다. 중단 수단은 없다.
		//
		// 예외를 던지면 해당 Actor는 격리되어 Stop된다. handler가 중간에 던졌다면
		// Actor 내부 상태가 부분적으로만 바뀌었을 수 있어, 다음 메시지를 계속
		// 처리하지 않는다.
		virtual void Handle(Message& message) = 0;
	};

	class ActorControlBlock;   // 내부 구현
	class ActorRef;

	// -----------------------------------------------------------------------
	// 진입점
	//
	//   첫 Spawn이 런타임을 기동시킨다. 그 전에 worker 수를 정하고 싶다면
	//   Runtime::SetWorkerCount를 먼저 부른다.
	// -----------------------------------------------------------------------

	// unique_ptr로 받는 것이 중요하다. shared_ptr이면 호출자가 Actor state에
	// 직접 접근할 수 있어 isolation invariant가 API 수준에서 뚫린다.
	//
	// 런타임이 종료되었으면 무효 ActorRef를 돌려준다.
	[[nodiscard]] ActorRef Spawn(std::unique_ptr<Actor> actor);

	// 진단용. 아직 Finalize되지 않은 Actor 수.
	//
	// [주의] 런타임이 프로세스 전역이므로 이 값도 전역 누적이다. 특정 Actor가
	// 정리되었는지 보려면 기준선을 잡고 그 차이를 본다.
	std::size_t LiveActorCount();

	// -----------------------------------------------------------------------
	// ActorRef
	//
	//   Actor에 대한 외부 접근 핸들. 복사 가능하다.
	//
	//   [주의] 이 핸들의 수명은 Actor의 수명을 결정하지 않는다. 마지막 ActorRef가
	//   사라져도 Actor는 살아 있다. 종료시키려면 Stop() 또는 런타임 종료가
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
		// false = Actor가 STOPPING/DEAD 이거나 런타임이 종료 상태다.
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
		friend ActorRef Spawn(std::unique_ptr<Actor> actor);
		explicit ActorRef(std::shared_ptr<ActorControlBlock> cb) noexcept;

		std::shared_ptr<ActorControlBlock> _cb;
	};

	// -----------------------------------------------------------------------
	// 정책 상수
	// -----------------------------------------------------------------------

	// 한 번 스케줄될 때 처리할 최대 메시지 수. 소진 후 메일박스가 비지 않았으면
	// 다시 스케줄된다. 한 액터가 worker를 독점하지 않게 끊는 장치다.
	// 측정에서 나온 값이 아니라 baseline이다.
	//
	// [주의] Runtime::INJECTION_POLL_INTERVAL과 상호작용한다. 그쪽 주석 참조.
	inline constexpr std::size_t DEFAULT_MESSAGE_BUDGET = 32;
}
