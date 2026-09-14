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

	void ThreadManager::DestroyTLS() {
		//지금은 할 일이 없다.
		//
		//send buffer는 여기서 정리하지 않는다. SendBufferManager가 함수 지역
		//thread_local이라 소멸자가 스레드 종료 시 current chunk 참조를 알아서
		//놓는다. 여기서 부르면 조금 더 일찍 놓일 뿐이고, 대신 ThreadManager가
		//SendBuffer에 의존하게 된다.
		//
		//스레드 종료 시 정리가 필요한 TLS가 생기면 여기에 넣는다.
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