#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <functional>

namespace MyUtils {
	class ThreadManager {
	public:
		ThreadManager();
		~ThreadManager();

		static void InitTLS();
		static void DestroyTLS() { };
		static void GetRegisteredActorAndProcess();
		static void DistributeOnTimeActorMessages();

		void Launch(std::function<void()> callback);
		void Join();

	private:
		std::mutex					_threadManagerLock;
		std::vector<std::thread>	_threads;
	};
}