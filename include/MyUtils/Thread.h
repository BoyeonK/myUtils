#pragma once

namespace MyUtils {
	class ThreadManager {
	public:
		ThreadManager();
		~ThreadManager();

		static void InitTLS();
		static void DestroyTLS() { };
		static void GetRegisteredActorAndProcess();
		//static void DoTimerQueueDistribution();

		void Launch(std::function<void()> callback);
		void Join();

	private:
		std::mutex			_threadManagerLock;
		std::vector<thread>	_threads;
	};
}