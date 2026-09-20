#pragma once

// 계약 위반(프로그래밍 오류)을 그 자리에서 중단시키는 진단 매크로.
//
// NDEBUG 가드가 없다. 릴리즈에서도 그대로 동작하며, 이는 의도된 것이다.
// 계약 위반은 조용히 넘어가는 것보다 그 자리에서 죽는 편이 낫다는 판단이다.
// 복구 가능한 실패에는 쓰지 말 것 -- 그런 건 반환값으로 알려야 한다.
//
// 공개 헤더이므로 매크로는 이것 하나뿐이고 접두사를 붙인다. 접두사 없는 이름
// (CRASH, DEBUG_BREAK 같은 것)은 소비자 쪽 매크로와 충돌한다.

namespace MyUtils::Detail {

	// 식과 위치를 진단으로 남기고 프로세스를 중단시킨다.
	// 구현이 <cstdio>/<cstdlib>와 컴파일러별 분기를 쓰므로 이 헤더에 두지 않는다.
	[[noreturn]] void AssertFailed(const char* expr, const char* file, int line) noexcept;
}

// do/while(0)로 감싸서 단일 문장처럼 동작하게 한다.
// 그냥 { }로 두면 `if (a) MYUTILS_ASSERT(b); else c;`가 깨진다.
#define MYUTILS_ASSERT(expr)                                                \
	do {                                                                    \
		if (!(expr))                                                        \
			::MyUtils::Detail::AssertFailed(#expr, __FILE__, __LINE__);     \
	} while (0)
