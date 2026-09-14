#pragma once

// 프로세스를 즉시 중단시키는 진단 매크로.
//
// NDEBUG 가드가 없다. 릴리즈에서도 그대로 동작하며, 이는 의도된 것이다.
// 계약 위반(프로그래밍 오류)은 조용히 넘어가는 것보다 그 자리에서 죽는 편이
// 낫다는 판단이다. 복구 가능한 실패에는 쓰지 말 것 -- 그런 건 반환값으로
// 알려야 한다.

#include <cstdint>

// -----------------------------------------------------------------------
// 플랫폼별 중단 수단
// -----------------------------------------------------------------------

#if defined(_MSC_VER) // [Windows] Visual Studio
	#include <intrin.h>
	#define DEBUG_BREAK()       __debugbreak()
	#define ANALYSIS_ASSUME(x)  __analysis_assume(x)

#elif defined(__linux__) || defined(__APPLE__) // [Linux/Mac] GCC, Clang
	// __builtin_trap()은 하드웨어 브레이크포인트/트랩을 발생시킵니다.
	// 혹은 asm volatile ("int $3")을 쓸 수도 있습니다.
	#define DEBUG_BREAK()       __builtin_trap()

	// 리눅스에서는 정적 분석 힌트가 필요 없으므로 빈 매크로로 둡니다.
	#define ANALYSIS_ASSUME(x)

#else // 그 외 환경
	#define DEBUG_BREAK()       ((void)0)
	#define ANALYSIS_ASSUME(x)
#endif

// -----------------------------------------------------------------------
// CRASH / ASSERT_CRASH
//
// do/while(0)로 감싸서 단일 문장처럼 동작하게 한다.
// 그냥 { }로 두면 `if (a) ASSERT_CRASH(b); else c;`가 깨진다.
// -----------------------------------------------------------------------

#define CRASH(msg)                                      \
	do {                                                \
		/* volatile을 써야 컴파일러가 최적화를 안 함 */ \
		std::uint32_t* crash = nullptr;                 \
		ANALYSIS_ASSUME(crash != nullptr);              \
		*(volatile std::uint32_t*)crash = 0xDEADBEEF;   \
	} while (0)

#define ASSERT_CRASH(expr)              \
	do {                                \
		if (!(expr)) {                  \
			DEBUG_BREAK();              \
			CRASH("ASSERT_CRASH");      \
		}                               \
	} while (0)
