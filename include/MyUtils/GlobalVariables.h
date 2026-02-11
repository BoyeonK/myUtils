#pragma once
#include <memory>
#include "concurrentqueue.h"
#include <random>

namespace MyUtils {
	//전방 선언띠
	class Actor;
	namespace Network { class SendBufferChunk; class SendBufferManager; }

	//Actor관련
	using GlobalQueueType = moodycamel::ConcurrentQueue<std::shared_ptr<MyUtils::Actor>>;
	extern GlobalQueueType* GActorQueue;
	extern thread_local std::shared_ptr<MyUtils::Actor> LCurrentActor;

	//Buffer관련
	extern class SendBufferManager* GSendBufferManager;
	extern thread_local std::shared_ptr<MyUtils::Network::SendBufferChunk> LSendBufferChunkRef;

	//난수 생성기
	extern thread_local mt19937 LRanGen;
}