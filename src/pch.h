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

// -----------------------------------------------------------------
// 2. 공용 CRASH / ASSERT 매크로
//
// 실제 정의는 MyUtils/Assert.h에 있다. 이 PCH에 직접 두면 여기에
// 의존하는 .cpp가 암묵적으로 windows.h와 `using namespace std;`까지
// 함께 끌어쓰게 되므로 분리했다.
// 새 코드는 PCH에 기대지 말고 MyUtils/Assert.h를 직접 include할 것.
// -----------------------------------------------------------------

#include "MyUtils/Assert.h"