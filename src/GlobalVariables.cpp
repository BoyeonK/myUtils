#include "MyUtils/GlobalVariables.h"

namespace MyUtils {
	GlobalQueueType* GActorQueue;

	thread_local shared_ptr<MyUtils::Actor> LCurrentActor = nullptr;
	thread_local shared_ptr<MyUtils::Network::SendBufferChunk> LSendBufferChunkRef = nullptr;
}