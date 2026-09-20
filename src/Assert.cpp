#include "MyUtils/Assert.h"

#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER)
	#include <intrin.h>
#endif

namespace MyUtils::Detail {

	void AssertFailed(const char* expr, const char* file, int line) noexcept {
		// 죽기 직전의 I/O라 버퍼링에 기대지 않고 바로 흘려보낸다.
		std::fprintf(stderr, "MyUtils assertion failed: %s\n  at %s:%d\n", expr, file, line);
		std::fflush(stderr);

		// [주의] 판정 기준은 플랫폼이 아니라 컴파일러다. `__linux__` / `__APPLE__`로
		// 가르면 Windows + MinGW·clang 조합이 어느 가지에도 걸리지 않아, 디버거를
		// 붙여도 멈추지 않는다.
		//
		// 디버거가 붙어 있으면 여기서 멈춘다. 그대로 진행시키면 아래 abort가 받는다.
		// 디버거가 없으면 MSVC는 이 지점에서 프로세스가 끝나고, GCC/Clang은 트랩이
		// 돌아오지 않는다.
#if defined(_MSC_VER)
		__debugbreak();
#elif defined(__GNUC__) || defined(__clang__)
		__builtin_trap();
#endif

		std::abort();
	}
}
