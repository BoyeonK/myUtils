#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

using namespace std;

#if defined(_MSC_VER) // [Windows] Visual Studio
#include <intrin.h>
#define DEBUG_BREAK()       __debugbreak()
#define ANALYSIS_ASSUME(x)  __analysis_assume(x)

#elif defined(__linux__) || defined(__APPLE__) // [Linux/Mac] GCC, Clang
#include <csignal>

// __builtin_trap()은 하드웨어 브레이크포인트/트랩을 발생시킵니다.
// 혹은 asm volatile ("int $3")을 쓸 수도 있습니다.
#define DEBUG_BREAK()       __builtin_trap() 

// 리눅스에서는 정적 분석 힌트가 필요 없으므로 빈 매크로로 둡니다.
#define ANALYSIS_ASSUME(x)  

#else // 그 외 환경
#define DEBUG_BREAK()       ((void)0)
#define ANALYSIS_ASSUME(x)
#endif


// -----------------------------------------------------------------
// 2. 공용 CRASH / ASSERT 매크로 정의
// -----------------------------------------------------------------

#define CRASH(msg)                              \
{                                               \
    /* volatile을 써야 컴파일러가 최적화를 안 함 */ \
    uint32_t* crash = nullptr;                  \
    ANALYSIS_ASSUME(crash != nullptr);          \
    *(volatile uint32_t*)crash = 0xDEADBEEF;    \
}

#define ASSERT_CRASH(expr)          \
{                                   \
    if (!(expr))                    \
    {                               \
        DEBUG_BREAK();              \
        CRASH("ASSERT_CRASH");      \
    }                               \
}