#include "MyUtils/GlobalVariables.h"
#include "MyUtils/Thread.h"
#include "MyUtils/Network.h"
#include "MyUtils/Actor.h"
#include "MyUtils/ScheduledActorMessagePoster.h"

namespace MyUtils {
	//Thread 및 Actor관련
	class ThreadManager* GThreadManager = nullptr;
	GlobalQueueType* GActorQueue = nullptr;
	class ActorEventScheduler* GActorEventScheduler = nullptr;
	thread_local shared_ptr<MyUtils::Actor> LCurrentActor = nullptr;
	thread_local uint32_t MyThreadID = 0;
	thread_local uint64_t LEndTickCount = 0;

	//Buffer관련
	Network::SendBufferManager* GSendBufferManager = nullptr;
	thread_local shared_ptr<MyUtils::Network::SendBufferChunk> LSendBufferChunkRef = nullptr;

	//난수 생성기
	thread_local mt19937 LRanGen;

	class CoreGlobal {
	public:
		CoreGlobal() {
			GThreadManager = new ThreadManager();
			GSendBufferManager = new Network::SendBufferManager();
			GActorQueue = new GlobalQueueType();
			GActorEventScheduler = new ActorEventScheduler();
		}

		~CoreGlobal() {
			delete GThreadManager;
			delete GSendBufferManager;
			delete GActorQueue;
			delete GActorEventScheduler;
		}
	} GCoreGlobal;
}