#include "MyUtils/GlobalVariables.h"
#include "MyUtils/Thread.h"
#include "MyUtils/Actor.h"
#include "MyUtils/ActorMessageScheduler.h"

namespace MyUtils {
	//Thread 및 Actor관련
	class ThreadManager* GThreadManager = nullptr;
	GlobalQueueType* GActorQueue = nullptr;
	class ActorMessageScheduler* GActorMessageScheduler = nullptr;
	thread_local Memory::MPSCQueue<TLTask*> TLTaskQueue;
	thread_local shared_ptr<MyUtils::Actor> LCurrentActor = nullptr;
	thread_local uint32_t MyThreadID = 0;
	thread_local uint64_t LEndTickCount = 0;

	//난수 생성기
	thread_local mt19937 LRanGen;

	class CoreGlobal {
	public:
		CoreGlobal() {
			GThreadManager = new ThreadManager();
			GActorQueue = new GlobalQueueType();
			GActorMessageScheduler = new ActorMessageScheduler();
		}

		~CoreGlobal() {
			delete GThreadManager;
			delete GActorQueue;
			delete GActorMessageScheduler;
		}
	} GCoreGlobal;
}