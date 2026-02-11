#pragma once

namespace MyUtils {
	class ThreadManager {
	public:
		ThreadManager();
		~ThreadManager();

		static void InitTLS();
		static void DestroyTLS() { };
		static void DoGlobalQueueWork();
		static void DoTimerQueueDistribution();

		void Launch(function<void()> callback);
		void Join();

	private:
		std::mutex			_threadManagerLock;
		std::vector<thread>	_threads;
	};
}