#pragma once
#include <cstdint>
#include <random>

namespace MyUtils {
	//전방 선언띠
	class ThreadManager;

	//Thread 관련
	extern class ThreadManager* GThreadManager;
	extern thread_local uint32_t MyThreadID;
	extern thread_local uint64_t LEndTickCount;

	//송신 버퍼는 전역 변수를 쓰지 않는다. MyUtils/SendBuffer.h 참조.
	//chunk 수명은 refcount가, current chunk는 thread_local SendBufferManager가 관리한다.

	//난수 생성기
	extern thread_local std::mt19937 LRanGen;
}
