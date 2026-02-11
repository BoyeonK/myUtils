#include "MyUtils/Thread.h"
#include "MyUtils/GlobalVariables.h"
#include "MyUtils/Actor.h"
#include <random>

namespace MyUtils {
	ThreadManager::ThreadManager() {
		InitTLS();
	}

	ThreadManager::~ThreadManager() {
		Join();
	}

	void ThreadManager::InitTLS() {
		static atomic<uint32_t> NxtThreadID = 1;
		MyThreadID = NxtThreadID.fetch_add(1);
		random_device rd;
		LRanGen.seed(rd());
	}

	void ThreadManager::DoGlobalQueueWork()	{
		while (LCurrentActor == nullptr) {
			shared_ptr<Actor> ActorRef;
			if (GActorQueue->try_dequeue(ActorRef)) {
				if (ActorRef != nullptr) {
					ActorRef->ProcessMyMessageBox();
				}
			}
			else {
				break;
			}
		}
	}

	void ThreadManager::DoTimerQueueDistribution() {

	}

	void ThreadManager::Launch(function<void()> callback) {
		lock_guard<mutex> guard(_threadManagerLock);
		_threads.push_back(thread([=] {
			InitTLS();
			callback();
			DestroyTLS();
		}));
	}

	void ThreadManager::Join() {
		for (thread& t : _threads) {
			if (t.joinable())
				t.join();
		}
		_threads.clear();
	}
}