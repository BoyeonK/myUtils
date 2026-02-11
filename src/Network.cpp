#include "MyUtils/GlobalVariables.h"
#include "MyUtils/Network.h"
#include "MyUtils/Memory.h"

namespace MyUtils::Network {
	void SendBufferChunk::Init() {
		_isOpen = false;
		_usedSize = 0;
	}

	shared_ptr<SendBuffer> SendBufferChunk::Open(uint32_t allocSize) {
		if (allocSize > FreeSize())
			return nullptr;

		_isOpen = true;

		shared_ptr<SendBuffer> SendBufferRef = MyUtils::Memory::ObjectPool<SendBuffer>::Acquire();
		SendBufferRef->Init(shared_from_this(), Index(), allocSize);
		return SendBufferRef;
	}

	void SendBufferChunk::Close(uint32_t writeSize) {
		ASSERT_CRASH(_isOpen == true);
		_isOpen = false;
		_usedSize += writeSize;
	}

	void SendBuffer::Init(shared_ptr<SendBufferChunk> chunkRef, unsigned char* index, uint32_t allocSize) {
		_chunkRef = chunkRef;
		_index = index;
		_allocSize = allocSize;
		_writeSize = 0;
	}

	void SendBuffer::Close(uint32_t writeSize) {
		ASSERT_CRASH(_allocSize >= writeSize);
		_writeSize = writeSize;
		_chunkRef->Close(writeSize);
	}

	shared_ptr<SendBuffer> SendBufferManager::Open(uint32_t allocSize) {
		//구조적으로 SEND_BUFFER_CHUNK크기보다 큰 바이트는 send할 수 없음
		ASSERT_CRASH(allocSize <= SEND_BUFFER_CHUNK_SIZE);

		if (LSendBufferChunkRef == nullptr || LSendBufferChunkRef->FreeSize() < allocSize) {
			shared_ptr<SendBufferChunk> newChunk = MyUtils::Memory::ObjectPool<SendBufferChunk>::Acquire();
			LSendBufferChunkRef = newChunk;
		}
		ASSERT_CRASH(LSendBufferChunkRef->IsOpen() == false);

		return LSendBufferChunkRef->Open(allocSize);
	}
}