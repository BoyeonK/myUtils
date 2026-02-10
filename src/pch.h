#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

using namespace std;

#define CRASH(msg)                          \
{                                           \
    uint32_t* crash = nullptr;              \
    __analysis_assume(crash != nullptr);    \
    *crash = 0xDEADBEEF;                    \
}

#define ASSERT_CRASH(expr)          \
{                                   \
    if (!(expr))                    \
    {                               \
        __debugbreak();             \
        CRASH("ASSERT_CRASH");      \
    }                               \
}