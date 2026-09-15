#pragma once

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace MyUtils {
	// 스레드를 띄우고 모아서 join하는 것이 전부다.
	//
	// 전면 재작성 대상이다. 이 위에 기능을 쌓지 말 것 --
	// 어떤 모양으로 갈지는 design/OPEN_QUESTIONS.md 의 Q-006에서 정한다.
	class ThreadManager {
	public:
		ThreadManager() = default;
		~ThreadManager();

		ThreadManager(const ThreadManager&) = delete;
		ThreadManager& operator=(const ThreadManager&) = delete;

		void Launch(std::function<void()> callback);
		void Join();

	private:
		std::mutex					_threadManagerLock;
		std::vector<std::thread>	_threads;
	};
}
