#pragma once
#include <memory>
#include <random>
#include "concurrentqueue.h"
#include "Memory.h"
#include "ThreadLocalTask.h"

namespace MyUtils {
	//전방 선언띠
	class Actor;
	class ThreadManager;
	class ActorMessageScheduler;
	namespace Network { class SendBufferChunk; class SendBufferManager; }

	//Thread 및 Actor관련
	extern class ThreadManager* GThreadManager;
	using GlobalQueueType = moodycamel::ConcurrentQueue<std::shared_ptr<MyUtils::Actor>>;
	extern GlobalQueueType* GActorQueue;
	extern class ActorMessageScheduler* GActorMessageScheduler;
	extern thread_local Memory::MPSCQueue<TLTask*> TLTaskQueue;
	extern thread_local std::shared_ptr<MyUtils::Actor> LCurrentActor;
	extern thread_local uint32_t MyThreadID;
	extern thread_local uint64_t LEndTickCount;

	//Buffer관련
	extern class Network::SendBufferManager* GSendBufferManager;
	extern thread_local std::shared_ptr<MyUtils::Network::SendBufferChunk> LSendBufferChunkRef;

	//난수 생성기
	extern thread_local std::mt19937 LRanGen;
}