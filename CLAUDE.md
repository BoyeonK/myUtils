# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 개요

`MyUtils`는 게임 서버류 애플리케이션의 런타임 기반 요소를 제공하는 C++17 정적 라이브러리(CMake 타겟 `MyUtils::MyUtils`)다. 액터/메시지 시스템, 락프리 오브젝트 풀, MPSC 큐, 타이머 스케줄러, 스레드 관리, 송신 버퍼 할당이 들어 있다. 실행 파일은 없고, 다른 프로젝트에서 `add_subdirectory` / `FetchContent`로 가져와 `target_link_libraries(... MyUtils::MyUtils)`로 링크해 쓰는 라이브러리다.

## 빌드

```sh
cmake -S . -B build                    # 최초 configure 시 FetchContent가 moodycamel/concurrentqueue v1.0.4를 받아온다
cmake --build build --config Debug     # 멀티 컨피그 생성기(Visual Studio)라 --config가 필요하다
ctest --test-dir build -C Debug --output-on-failure
```

단일 테스트만 돌릴 때는 `ctest --test-dir build -C Debug -R SendBuffer`, 또는 실행 파일을
직접 실행한다(`build/tests/Debug/SendBufferTest.exe`). 실행 파일이 통계 요약까지 출력하므로
직접 실행하는 쪽이 정보가 많다.

테스트는 이 레포를 **단독으로 빌드할 때만** 켜진다. 다른 프로젝트가 `add_subdirectory`로
가져다 쓸 때는 꺼진다(`MYUTILS_BUILD_TESTS`).

lint 단계와 install 규칙은 없다.

## 인코딩 (중요)

소스와 헤더는 전부 **BOM 없는 UTF-8**이고 주석은 한국어다. MSVC는 기본적으로 시스템
코드 페이지(한국어 환경이면 949)로 읽기 때문에, 한글 주석의 마지막 바이트가 CP949
lead byte로 오해되어 **줄바꿈까지 삼키고 다음 줄이 주석에 먹히는 일이 실제로 발생한다**
(예: "양" = `EC 96 91` → `91`이 lead byte가 되어 `\n`을 소비). 증상이 "멀쩡히 있는 멤버를
찾을 수 없다"는 C2039 에러로 나타나므로 원인을 찾기 어렵다.

`CMakeLists.txt`가 `target_compile_options(MyUtils PUBLIC /utf-8)`로 이를 막고 있다.
**이 옵션을 지우지 말 것.** 공개 헤더에도 한글 주석이 있어 소비자에게도 전파해야 하므로
PUBLIC이다.

## 헤더 작성 규칙 (여기서 실수가 잦음)

- `src/pch.h`는 **PRIVATE** 미리 컴파일된 헤더다. `using namespace std;`를 하고, `<windows.h>`를 끌어오며(`NOMINMAX`가 없어서 `min`/`max` 매크로가 살아 있다), `MyUtils/Assert.h`를 include한다. 레거시 `.cpp`들은 이 PCH에 암묵적으로 의존한다 — `shared_ptr`을 네임스페이스 없이 쓰고, 아무것도 include하지 않은 채 `ASSERT_CRASH`를 호출한다.
- **새 코드는 PCH에 기대지 말 것.** `CRASH` / `ASSERT_CRASH` / `DEBUG_BREAK`는 `include/MyUtils/Assert.h`에 있으니 직접 include한다. PCH를 통해 쓰면 `windows.h`와 `using namespace std;`까지 그 TU에 딸려 들어온다. `src/SendBuffer.cpp`가 이 방식을 따르며, PCH 없이도 단독으로 컴파일된다.
- `ASSERT_CRASH`에는 `NDEBUG` 가드가 없어 **릴리즈에서도 죽는다.** 계약 위반(프로그래밍 오류)에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- 반면 `include/MyUtils/`의 공개 헤더는 PCH에 의존하면 **안 된다**. 반드시 자립적이어야 한다: `#pragma once`, `<memory>`/`<atomic>` 등 필요한 include 명시, 이름은 전부 `std::`로 한정. 과거에 `#pragma once` 누락과 `std::` 누락으로 각각 별도의 수정 커밋이 필요했던 지점이다.
- `src/Memory.cpp`와 `src/ThreadLocalTask.cpp`는 의도적으로 비어 있다. 해당 컴포넌트는 헤더 온리 템플릿/인터페이스지만 `CMakeLists.txt`에는 그대로 등록되어 있다. 새 소스 파일을 추가하면 `add_library` 목록에 직접 넣어야 한다.
- 주석과 커밋 메시지는 한국어다.

## 아키텍처

### 전역 상태와 초기화 (`GlobalVariables.h` / `.cpp`)

프로세스 전역 싱글턴은 전부 raw 포인터 전역 변수다. `GlobalVariables.h`에서 `extern`으로 선언하고 `GlobalVariables.cpp`에서 정의하며, 같은 파일의 파일 스코프 객체 `CoreGlobal GCoreGlobal`이 정적 초기화 시점에 `new`하고 종료 시 `delete`한다: `GThreadManager`, `GActorQueue`, `GActorMessageScheduler`.

스레드별 상태도 같은 파일 쌍에 `thread_local` 전역으로 있다: `LCurrentActor`, `MyThreadID`, `LEndTickCount`, `LRanGen`, `TLTaskQueue`. `ThreadManager::InitTLS()`가 `MyThreadID`를 배정하고 `LRanGen`을 시드하며, `ThreadManager` 생성자(메인 스레드용)와 `ThreadManager::Launch`로 띄운 모든 스레드의 시작 지점에서 호출된다. 짝이 되는 `DestroyTLS()`는 현재 비어 있다(스레드 종료 시 정리가 필요한 TLS가 생기면 거기에 넣는다).

송신 버퍼는 전역 변수도, `DestroyTLS` 훅도 쓰지 않는다. chunk 수명은 refcount가, current chunk는 함수 지역 `thread_local SendBufferManager`가 관리하며 그 소멸자가 스레드 종료 시 알아서 반납한다. **`SendBuffer` 계열은 레거시 코드와 양방향 모두 분리되어 있다** — 표준 라이브러리, `concurrentqueue.h`, `MyUtils/Assert.h` 외에는 아무것도 참조하지 않고, 레거시 쪽에서도 참조하지 않는다. 이 상태를 유지할 것.

`GlobalVariables.h`는 include를 가볍게 유지하려고 `Actor`, `ThreadManager`, `ActorMessageScheduler`를 전방 선언한다. 이 상태를 유지할 것.

### 액터 / 메시지 펌프 (`Actor.h`, `Actor.cpp`, `Thread.cpp`)

핵심 동시성 계약: **하나의 액터는 동시에 최대 한 스레드에서만 실행된다.** 락 없이 이를 보장한다.

- 각 `Actor`는 `MPSCQueue<shared_ptr<Message>> _messages`와 `atomic<bool> _isExecuting`을 갖는다.
- `Actor::Push`는 메시지를 넣은 뒤 `_isExecuting`을 false→true로 CAS한다. CAS에 성공한 스레드만 액터의 `shared_ptr`을 전역 `GActorQueue`(`moodycamel::ConcurrentQueue`)에 넣는다. 따라서 한 액터는 전역 큐에 최대 한 번만 들어간다.
- 워커 스레드는 `ThreadManager::GetRegisteredActorAndProcess()`를 호출하고, 여기서 액터를 꺼내 `ProcessMyMessageBox()`를 실행한다. 이 함수는 메일박스를 비우고 `_isExecuting`을 내린 뒤, 비우기와 내리기 사이에 새 메시지가 들어온 경합을 다시 확인해서 — 큐에 재등록하는 대신 — 그 자리에서 다시 점유한다. 처리 중에는 `LCurrentActor`가 설정되고, `GetRegisteredActorAndProcess`의 루프는 `LCurrentActor == nullptr` 조건으로 막혀 있어 액터 처리의 재진입을 방지한다.
- `Message`가 추상 베이스(`Process()`)이고, `Task`가 유일한 구현체로 `std::function<void()>` 또는 `weak_ptr<T>` + 멤버 함수 포인터 + 바인딩된 인자 튜플을 감싼다. weak_ptr 형태가 핵심이다: 태스크가 소유자를 살려두지 않으며, 소유자가 이미 소멸했으면 조용히 아무 일도 하지 않는다. `Actor::PostTask`가 `shared_from_this()`를 쓰므로 액터는 반드시 `shared_ptr`로 보유되어야 한다.

### 타이머 (`ActorMessageScheduler.h` / `.cpp`)

`PostTaskAfter(tickAfter, ...)`는 `GActorMessageScheduler->Reserve(...)`로 흘러간다. 여기서 `steady_clock` 기준 절대 밀리초 tick을 찍고, 뮤텍스로 보호되는 대기 벡터 `_toBeAddedMessages`에 넣는다. 정렬용 `priority_queue _orderedMessages`는 **락으로 보호되지 않는다** — 오직 `AddAndDistribute(now)` 안에서만 접근하기 때문이다. 이 함수는 락을 잡고 대기 벡터를 병합한 뒤, 시간이 된 메시지를 모두 꺼내 소유 액터의 메일박스로 `Push`한다. 그래서 `AddAndDistribute`(= `ThreadManager::DistributeOnTimeActorMessages()`)는 **정확히 한 스레드에서만** 돌려야 한다. 시간이 됐지만 소유자 `weak_ptr`이 더 이상 lock되지 않는 메시지는 버려진다.

### 오브젝트 풀 (`Memory.h`)

`ObjectPool<T>`는 인스턴스를 만들 수 없는(생성자 `delete`) 정적 전용 클래스 템플릿이며, 2계층 프리 리스트 구조다:

- 1계층: `thread_local std::vector<T*>` 로컬 프리 리스트, `MAX_LOCAL_SIZE`(2000)만큼 reserve.
- 2계층: 함수 지역 `static moodycamel::ConcurrentQueue<T*>` 전역 풀. `try_dequeue_bulk` / `enqueue_bulk`로 `BATCH_SIZE`(500) 단위 리필/플러시.

메모리는 raw(`::operator new(sizeof(T))`)로 잡고 OS에 되돌려주지 않는다. 생성은 placement new, 소멸은 명시적 소멸자 호출이다. `Acquire()`는 커스텀 deleter(`~T()` 실행 후 블록 반납)를 단 `shared_ptr<T>`를 돌려주고, `AcquireRaw`/`ReturnRaw`는 `shared_ptr`이 과한 곳에서 쓰는 수동 쌍이다. 슬롯이 재사용되므로 **풀에 들어가는 타입은 생성자에서 자기 상태를 전부 다시 초기화해야 한다** — 메모리가 0으로 채워져 있다고 가정하지 말 것.

`MPSCQueue<T>`는 stub 노드를 쓰는 Vyukov 스타일 침습적 큐로, `ObjectPool<Node>` 위에 구현되어 있다. `_head`(생산자, `exchange` acq_rel)와 `_tail`(단일 소비자)은 false sharing을 피하려고 각각 `alignas(64)`다. `Dequeue`는 반드시 소비자 하나만 호출해야 한다.

### 송신 버퍼 (`SendBuffer.h` / `SendBuffer.cpp`)

thread-local bump allocator + refcounted arena 구조다. 사용 흐름은 `Reserve` → 쓰기 → `Commit` → I/O.

```
Reserve(capacity) → SendBuffer (이동 전용, mutable)
                  → Commit(actualSize) → SendView (복사 가능, immutable)
```

핵심 불변식은 **"refcount == 0 이면 아무도 이 chunk를 모른다"**이며, 이게 성립하려면
`SendBufferManager`가 Current Chunk에 대한 `ChunkRef`를 **하나 들고 있어야 한다**.
outstanding `SendView`만 세면, 마지막 I/O가 끝나는 순간 아직 사용 중인 chunk가 pool로
반환되어 두 worker가 같은 chunk에 동시에 bump allocation하게 된다. 이 참조를 제거하지 말 것.

다른 구조적 결정들:

- **`_offset`은 동기화하지 않는다.** 한 시점에 하나의 owner thread만 allocation하며, 소유권 이전은 **반드시 ChunkPool을 경유**해서 happens-before를 확보한다. worker 사이에서 chunk를 직접 넘기면 이 보장이 깨진다.
- **`Reset()`은 반환이 아니라 획득 시점에** 부른다. `_offset`을 만지는 주체를 항상 현재 owner 하나로 묶기 위해서다.
- **tail reclaim**은 두 조건이 모두 참일 때만 한다 — ① 내 TLS manager의 Current가 이 chunk이고(= 내가 owner임의 구조적 증명), ② `chunk.Offset() == 내시작 + 예약크기`(= 내 뒤에 다른 allocation 없음). Commit 없이 소멸해도 같은 경로로 롤백된다.
- **ChunkPool은 `shared_ptr`로 소유되고 chunk의 deleter가 그 지분을 캡처한다.** 덕분에 pool이 자기가 내준 chunk 전부보다 오래 살아서, 임베딩하는 쪽의 shutdown 순서와 무관하게 반환이 안전하다. 유휴 chunk는 raw 포인터로만 보관하므로 순환 참조는 없다.
- `LARGE_ALLOCATION_THRESHOLD`를 넘는 요청은 전용 chunk로 가며 current chunk를 교체하지 않는다. 이 값은 `DEFAULT_CHUNK_CAPACITY`보다 **작게** 둘 수 있는 정책 노브다(큰 요청 하나가 current chunk의 남은 공간을 버리는 것을 막는다).
- `SendView`는 복사 가능하므로 브로드캐스트와 scatter/gather send(여러 chunk의 View를 한 번의 `WSASend`에)를 그대로 지원한다. 대신 둘 다 chunk 고정(pinning)을 증폭시킨다.
- 계측(`SnapshotStats`)은 thread_local 블록이 자기 생성자/소멸자에서 등록·집계하므로 hot path에 동기화가 없다. 살아 있는 thread의 카운터를 잠금 없이 읽는 것은 의도된 benign race다.

`DEFAULT_CHUNK_CAPACITY`(16 KB), `LARGE_ALLOCATION_THRESHOLD`, `MAX_SEND_BUFFER_SIZE`, `DEFAULT_POOL_IDLE_LIMIT`은 전부 튜닝 값이며 correctness와 무관하다. 계측 결과를 보고 조정할 것.

### 스레드 로컬 태스크 (`ThreadLocalTask.h`) — 작업 중

`TLTask`(추상: `Process()` + `ClearAfterProcess()`)와 `thread_local TLTaskQueue`는 선언만 되어 있고 아직 어떤 루프에서도 소비하지 않는다. 특정 태스크를 특정 스레드에 고정하기 위해 진행 중인 작업이다. `TLTask::ClearAfterProcess`가 있는 이유는 인스턴스가 `ObjectPool`에서 나오기 때문이며, 구현체는 자기 상태를 스스로 정리해야 한다.
