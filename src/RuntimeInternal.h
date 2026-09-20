#pragma once

// 런타임 몸체의 비공개 구조. 소비자에게 노출되지 않는다.
//
//   RuntimeBody
//      ├── WorkerPool      스레드 · 큐 · 수면 프로토콜
//      └── ActorRegistry   Actor의 논리적 수명
//
// 몸체를 둘로 가르는 기준은 확장 축이다. 추후 I/O·임의 함수자가 붙는 곳은
// WorkerPool뿐이고 ActorRegistry는 그대로 남는다.
//
// WorkerPool은 Actor를 모른다. 큐에 담기는 것은 Task이고, ActorControlBlock이
// 그중 하나일 뿐이다. 이 경계를 허물면 다른 종류의 작업을 얹을 때 pool 내부를
// 다시 설계해야 한다.

#include "MyUtils/Runtime.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "concurrentqueue.h"

namespace MyUtils::Actors {
	class ActorControlBlock;
}

namespace MyUtils::Runtime::Detail {

	// =======================================================================
	// Task — pool이 실행하는 것의 유일한 형태
	// =======================================================================
	struct Task {
		Task() = default;
		virtual ~Task() = default;

		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;

		// worker가 이것만 부른다. 배치 크기 같은 정책은 구현자의 몫이다 —
		// pool은 Actor의 message budget을 알지 못한다.
		virtual void Execute() = 0;

		// 계측용 등록 시각. Submit이 쓰고 worker 쪽 구현이 읽는다.
		// 순서는 큐가 보장한다.
		std::atomic<std::uint64_t> enqueuedAtUs{ 0 };
	};

	using TaskPtr = std::shared_ptr<Task>;

	// =======================================================================
	// 계측 접근자
	// =======================================================================

	// 호출 thread의 계측 블록. 첫 호출 시 스스로 등록된다.
	Stats& ThreadStats() noexcept;

	// 계측 전역(레지스트리·락·누적분)을 강제로 생성시킨다.
	//
	// [필수] RuntimeBody 생성자가 worker를 만들기 전에 부른다. 이유는 그 자리의
	// 주석에 있다. 지우면 프로세스 종료 시점에만, 비결정적으로 깨진다.
	void TouchStatsGlobals() noexcept;

	std::uint64_t NowUs() noexcept;

	// =======================================================================
	// WorkerPool
	// =======================================================================
	class WorkerPool {
	public:
		explicit WorkerPool(std::size_t workerCount);
		~WorkerPool();

		WorkerPool(const WorkerPool&) = delete;
		WorkerPool& operator=(const WorkerPool&) = delete;

		// 생성과 기동을 나눈 것은 의도적이다. RuntimeBody 생성자가 멤버 초기화
		// 이후에 계측 전역을 먼저 세운 뒤 스레드를 띄워야 하기 때문이다.
		void Start();

		// task를 만드는 유일한 경로다. **어느 큐에 넣을지 고르는 일까지** 여기서
		// 한다. 큐를 직접 건드리는 경로를 만들면 그 경로에서만 worker가 깨지 않는다.
		//
		// false = 종료 중이라 받지 않았다.
		bool Submit(TaskPtr task);

		void StopAndJoin();      // 종료 2·3단계
		void DiscardPending();   // 종료 4단계. StopAndJoin 이후에만 부른다

		std::size_t WorkerCount() const noexcept { return _workerCount; }

	private:
		// worker-local work-stealing deque.
		//
		//   owner  : push_bottom / pop_bottom (LIFO — locality)
		//   thief  : steal_top               (FIFO — 가장 오래된 것부터)
		//
		// Chase-Lev lock-free가 아니라 뮤텍스다. 경합 제거의 본체는 락을 1개에서
		// N개로 쪼개는 것이고, 스케줄러 연산은 이미 배치(32건)당 1회라 뮤텍스
		// 비용이 묻힌다.
		struct LocalDeque {
			std::mutex lock;
			std::deque<TaskPtr> items;
		};

		void WorkerLoop(std::size_t index);

		// tick % INJECTION_POLL_INTERVAL == 0 이면 injection을 먼저 본다
		bool NextTask(std::size_t index, std::size_t tick, TaskPtr& out);

		// sleep 직전 재확인용. 자기 deque / injection / 남의 deque를 전부 본다.
		bool AcquireAnywhere(std::size_t index, TaskPtr& out);

		bool PopLocal(std::size_t index, TaskPtr& out);
		bool TrySteal(std::size_t index, TaskPtr& out);

		moodycamel::ConcurrentQueue<TaskPtr> _injection;
		std::vector<std::unique_ptr<LocalDeque>> _local;

		std::mutex _sleepLock;
		std::condition_variable _wakeup;

		// 유휴(수면 중이거나 수면 영역에 진입한) worker 수.
		//
		// [필수] 증감은 반드시 _sleepLock 안에서 한다. 락 밖에서 세면 producer가
		// 0을 읽고 notify를 건너뛴 직후 worker가 잠드는 창이 생긴다. 락 안에서
		// 세면 "producer가 0을 읽었다"가 "수면 영역에 들어와 있는 worker가 없다"와
		// 같은 뜻이 된다.
		std::atomic<std::size_t> _idleCount{ 0 };

		std::atomic<bool> _stopping{ false };

		std::size_t _workerCount = 0;
		std::vector<std::thread> _workers;
	};

	// =======================================================================
	// ActorRegistry
	//
	//   Actor의 논리적 수명을 쥔다. 모든 멤버의 정의는 Actors.cpp에 있다 —
	//   ActorControlBlock의 완전한 타입이 필요하기 때문이다.
	// =======================================================================
	class ActorRegistry {
	public:
		ActorRegistry() = default;

		ActorRegistry(const ActorRegistry&) = delete;
		ActorRegistry& operator=(const ActorRegistry&) = delete;

		// 등록 허용 확인과 등록을 같은 락 아래에서 선형화한다.
		// true면 종료 시 snapshot에 반드시 포함되고, false면 등록되지 않는다.
		bool TryRegister(std::shared_ptr<Actors::ActorControlBlock> cb);
		void Unregister(const std::shared_ptr<Actors::ActorControlBlock>& cb);

		bool IsAccepting() const noexcept { return _accepting.load(); }
		std::size_t LiveActorCount() const;

		void CloseGate();     // 종료 1단계
		void FinalizeAll();   // 종료 5·6단계. worker join 이후에만 부른다

	private:
		mutable std::mutex _lock;
		std::unordered_set<std::shared_ptr<Actors::ActorControlBlock>> _registry;
		std::atomic<bool> _accepting{ true };
	};

	// =======================================================================
	// RuntimeBody — 프로세스 전역 몸체
	// =======================================================================
	class RuntimeBody {
	public:
		RuntimeBody();
		~RuntimeBody();

		RuntimeBody(const RuntimeBody&) = delete;
		RuntimeBody& operator=(const RuntimeBody&) = delete;

		// 멱등. 순서가 계약이다 — design/actor-runtime.md §8
		void Shutdown();

		WorkerPool& Pool() noexcept { return _pool; }
		ActorRegistry& Registry() noexcept { return _registry; }

	private:
		std::atomic<bool> _shutdownStarted{ false };

		// [주의] 선언 순서가 파괴 순서를 정한다. Shutdown이 worker를 join한 뒤에야
		// 멤버가 파괴되므로 둘 사이에 의존이 남지 않지만, 순서를 바꿀 이유도 없다.
		WorkerPool _pool;
		ActorRegistry _registry;
	};

	// 첫 호출이 런타임을 기동시킨다. 기동 경합은 magic static이 처리한다.
	RuntimeBody& Instance();
}
