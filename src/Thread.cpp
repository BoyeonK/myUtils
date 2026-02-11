#include "MyUtils/Thread.h"
#include "MyUtils/GlobalVariables.h"
#include "MyUtils/ActorMessageScheduler.h"
#include "MyUtils/Actor.h"
#include <random>

using namespace std;

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

	void ThreadManager::GetRegisteredActorAndProcess()	{
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

	void ThreadManager::DistributeOnTimeActorMessages() {
		using namespace std::chrono;
		const uint64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		GActorMessageScheduler->AddAndDistribute(now);
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