#include "MyUtils/Thread.h"

#include <utility>

namespace MyUtils {
	ThreadManager::~ThreadManager() {
		Join();
	}

	void ThreadManager::Launch(std::function<void()> callback) {
		std::lock_guard<std::mutex> guard(_threadManagerLock);
		_threads.emplace_back(std::move(callback));
	}

	void ThreadManager::Join() {
		for (std::thread& t : _threads) {
			if (t.joinable())
				t.join();
		}
		_threads.clear();
	}
}
