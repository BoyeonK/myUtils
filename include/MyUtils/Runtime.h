#pragma once

// 프로세스 전역 런타임.
//
//   worker thread pool과 그 위에서 도는 실행 주체가 하나의 몸체다.
//   지금 그 위에 있는 것은 Actor뿐이다(MyUtils/Actor.h).
//
//   (선택) Runtime::SetWorkerCount(n)   기동 전에만
//          Actors::Spawn(...)           이 시점에 자동으로 기동한다
//          Runtime::Shutdown()          몸체 전체를 종료. 멱등
//
// 초기화 훅이 없다. 무엇을 먼저 불러야 한다는 계약이 없고, SetWorkerCount를
// 부르지 않는 것이 정상 경로다.
//
// 이 헤더를 건드리기 전에 반드시 알아야 할 것:
//   - 몸체는 프로세스에 하나뿐이고, 1회 기동해서 1회 종료한다. 재기동은 없다.
//   - 부분 종료가 없다. pool만 멈추거나 Actor만 멈추는 경로는 존재하지 않는다.
//
// 스케줄러 구조, 수면/기상 프로토콜, 종료 시퀀스는 design/actor-runtime.md 에 있다.

#include <cstddef>
#include <cstdint>

namespace MyUtils::Runtime {

	// -----------------------------------------------------------------------
	// 구성
	// -----------------------------------------------------------------------

	// worker 수를 지정한다. 부르지 않으면 std::thread::hardware_concurrency()를
	// 쓰고, 그것이 0을 보고하면 1로 내린다.
	//
	// [계약] 런타임이 이미 기동했다면 계약 위반이다. 설정이 조용히 무시되는 것이
	//        최악이므로 그 자리에서 죽인다. count >= 1 이어야 한다.
	//
	// 기동 전 반복 호출은 마지막 값이 이긴다.
	void SetWorkerCount(std::size_t count);

	// 유효 worker 수. 지정하지 않았다면 기본값을 계산해서 돌려준다.
	//
	// [주의] 이 함수는 런타임을 기동시키지 않는다. 기동 전에 불러도 안전하며,
	//        부른 뒤에도 SetWorkerCount를 쓸 수 있다.
	std::size_t WorkerCount() noexcept;

	// 몸체 전체를 종료한다. 부분 종료는 없다.
	//
	// 여러 번 불러도 안전하다. 프로세스 종료 시 자동으로 수행되므로 반드시
	// 불러야 하는 것은 아니다.
	//
	// [주의] 실행 중인 작업이 끝날 때까지 block한다. 중단 수단은 없다 — 긴
	//        handler는 곧 긴 종료다. 종료 지연을 제한해야 한다면 handler 쪽에
	//        시한을 두는 것이 유일한 수단이다.
	//
	// 종료 이후 Actors::Spawn은 무효 핸들을, Send는 false를 돌려준다.
	void Shutdown();

	// -----------------------------------------------------------------------
	// 정책 상수
	// -----------------------------------------------------------------------

	// worker가 몇 번에 한 번 injection queue를 local deque보다 먼저 볼 것인가.
	//
	// 이게 없으면 local deque가 계속 차 있는 동안 injection을 한 번도 확인하지
	// 않아 외부 스레드·I/O completion의 작업이 무한정 밀린다. 최적값이 아니라
	// baseline이다.
	//
	// [주의] Actors::DEFAULT_MESSAGE_BUDGET과 상호작용한다. Actor 한 번 실행이
	// 최대 32건이므로 injection 확인 간격은 최악의 경우 메시지 1,952건이다.
	// 둘 중 하나를 바꾸면 다른 쪽의 의미도 바뀐다.
	inline constexpr std::size_t INJECTION_POLL_INTERVAL = 61;

	// -----------------------------------------------------------------------
	// 계측
	//
	//   hot path는 자기 thread의 블록만 만지므로 동기화가 없다.
	//   블록의 생성자/소멸자가 등록과 집계를 처리하므로, thread가 죽어도
	//   누적값을 잃지 않고 별도 정리 훅도 필요 없다.
	//
	//   [주의] pool 지표와 Actor 지표가 한 블록에 있다. 몸체가 하나의 컴포넌트
	//   단위이기 때문이며, design/instrumentation.md 의 "컴포넌트마다 자기 계측"을
	//   어긴 것이 아니다.
	// -----------------------------------------------------------------------
	struct Stats {
		// --- Send ---
		std::uint64_t sendAccepted = 0;
		std::uint64_t sendRejected = 0;                 // Actor가 STOPPING/DEAD

		// --- 스케줄링 ---
		std::uint64_t scheduleCount = 0;                // IDLE -> SCHEDULED 성공
		std::uint64_t scheduleAbortedSystemStopping = 0;// 런타임 종료 중이라 등록 실패
		std::uint64_t runCount = 0;                     // 배치 실행 횟수
		std::uint64_t messagesHandled = 0;

		// task 등록부터 worker가 집기까지 걸린 시간의 합.
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
		// injectionPush만 늘면 worker가 만든 task가 local로 안 가고 있다는 뜻이며,
		// 라우팅 테스트들이 이 두 값으로 경계를 확인한다.
		std::uint64_t localPushCount = 0;
		std::uint64_t injectionPushCount = 0;
	};

	// 모든 thread의 값을 합산한 스냅샷. 이미 종료된 thread의 누적분도 포함한다.
	//
	// [주의] 살아 있는 thread의 카운터를 동기화 없이 읽으므로 엄밀히는 data race다.
	// 통계 목적이라 허용한다. 정확한 값이 필요하면 대상 thread를 join한 뒤 읽을 것.
	//
	// 이 함수도 런타임을 기동시키지 않는다.
	Stats SnapshotStats();
}
