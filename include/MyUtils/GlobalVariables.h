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

	//Thread 및 Actor관련
	extern class ThreadManager* GThreadManager;
	using GlobalQueueType = moodycamel::ConcurrentQueue<std::shared_ptr<MyUtils::Actor>>;
	extern GlobalQueueType* GActorQueue;
	extern class ActorMessageScheduler* GActorMessageScheduler;
	extern thread_local Memory::MPSCQueue<TLTask*> TLTaskQueue;
	extern thread_local std::shared_ptr<MyUtils::Actor> LCurrentActor;
	extern thread_local uint32_t MyThreadID;
	extern thread_local uint64_t LEndTickCount;

	//Buffer관련은 MyUtils/SendBuffer.h 참조.
	//Chunk 수명은 refcount가, current chunk는 thread_local SendBufferManager가
	//들고 있으므로 전역 변수가 필요 없다.

	//난수 생성기
	extern thread_local std::mt19937 LRanGen;
}