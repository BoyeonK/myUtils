#include "MyUtils/GlobalVariables.h"
#include "MyUtils/Thread.h"

namespace MyUtils {
	//Thread 관련
	class ThreadManager* GThreadManager = nullptr;
	thread_local uint32_t MyThreadID = 0;
	thread_local uint64_t LEndTickCount = 0;

	//난수 생성기
	thread_local mt19937 LRanGen;

	class CoreGlobal {
	public:
		CoreGlobal() {
			GThreadManager = new ThreadManager();
		}

		~CoreGlobal() {
			delete GThreadManager;
		}
	} GCoreGlobal;
}
