#include "MyUtils/Profiling.h"

#include <atomic>

namespace MyUtils {

	namespace {
		// relaxed로 충분하다. 이 플래그는 "지금부터 재면 좋겠다"는 힌트이지
		// 다른 메모리 접근과의 순서를 요구하지 않는다. 켜는 순간 경계에서
		// 몇 건이 새거나 빠질 수 있지만 통계 목적이라 무해하다.
		std::atomic<bool> GProfilingEnabled{ false };
	}

	bool IsProfilingEnabled() noexcept {
		return GProfilingEnabled.load(std::memory_order_relaxed);
	}

	void SetProfilingEnabled(bool enabled) noexcept {
		GProfilingEnabled.store(enabled, std::memory_order_relaxed);
	}
}
