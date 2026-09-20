# CLAUDE.md

This file is the single source of shared project context and working rules for coding agents in this repository.

## 개요

`MyUtils`는 게임 서버류 애플리케이션의 런타임 기반 요소를 제공하는 C++17 정적 라이브러리(CMake 타겟 `MyUtils::MyUtils`)다. 실행 파일은 없고, 다른 프로젝트에서 `add_subdirectory` / `FetchContent`로 가져와 `target_link_libraries(... MyUtils::MyUtils)`로 링크해 쓴다.

현재 구성 요소는 둘이다.

* **송신 버퍼** (`SendBuffer.h`) — thread-local bump allocator + refcounted chunk. 완성된 컴포넌트다.
* **Actor Runtime** (`Actor.h`) — MPSC mailbox + atomic 상태 머신 + worker pool. 스케줄러는 **global injection queue + worker-local work-stealing deque**다. 실행 규약은 `design/actor-runtime.md`.

둘 다 자기 계측을 갖고 있다. `MyUtils::Network::SnapshotStats()` / `MyUtils::Actors::SnapshotStats()`로 조회하며, 두 테스트 실행 파일이 종료 시 요약을 출력한다. 비용은 전부 chunk당 또는 배치당이라 항상 켜져 있다 — 런타임 스위치가 없다.

액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러, `ThreadManager`, 그리고 전역 변수 일체를 **2026-09-15에 전부 제거했다.** 전면 재작성 대상이었고, 그 위에 무언가를 쌓는 것보다 비우고 다시 세우는 편이 낫다는 판단이었다. 필요하면 git 히스토리(`2ce7019` 이전)에서 참고할 수 있다.

## 공통 작업 원칙

* Correctness를 micro-optimization보다 우선한다.
* 측정 근거가 없는 성능 최적화를 먼저 추가하지 않는다.
* 이미 동작하는 단순한 구조를 추측만으로 복잡하게 만들지 않는다.
* 요청받지 않은 대규모 리팩터링이나 범위 확장을 함께 수행하지 않는다.
* 문서에서 확인한 사실, 코드에서 확인한 사실, 자신의 추론을 구분한다.
* 기존 설계와 코드가 어긋나면 임의로 한쪽을 맞추지 말고 먼저 차이를 보고한다.

### 작업 절차

코드를 수정하기 전에 관련 코드와 문서를 읽고, 현재 동작과 설계 의도 및 영향을 받는
invariant와 테스트를 확인한다. 구체적인 문서 선택 순서는 아래 `문서 구조` 절을 따른다.

수정한 뒤에는 가능한 범위에서 관련 테스트를 실행한다. 테스트를 실행하지 않았거나
실행할 수 없었다면 그 사실을 명시한다.

### AI Workflow

일반적인 질문, 코드 조사, 설명 요청 및 수정 요청은 기존 작업 절차에 따라 처리한다.

사용자가 요청의 시작에 다음 문구를 명시한 경우에만 정형화된 AI 개발 workflow에 진입한다.

`워크플로우 시작:`

이 문구가 감지되면 다른 작업을 수행하기 전에 `workflow.md`를 읽고,
그 문서에 정의된 절차와 상태 전이 규칙을 따른다.

Codex 또는 Claude 세션을 시작했다는 사실만으로 workflow에 진입하지 않는다.

### 성능과 계측

성능 관련 변경은 가능하면 실제 측정 결과를 근거로 한다. 문서와 주석에 적힌 정책 상수는
**측정에서 나온 최적값이 아니라 baseline**이므로 그렇게 취급한다.

구현 수단을 고를 때는 **검증된 라이브러리 → mutex 기반 → 직접 작성한 lock-free** 순으로
검토한다. 세 번째 칸은 측정 근거가 있을 때만 연다.

### 범위 관리

작업 중 발견한 별도 문제는 현재 작업의 correctness에 직접 영향을 주는지 구분한다.
직접 영향을 주면 현재 작업과 함께 보고하고 필요하면 수정한다. 독립적인 문제라면 별도
관찰 또는 후속 작업 후보로 분리한다. 새로운 설계 문제를 발견했다는 이유만으로 구현
범위를 즉시 넓히지 않는다.

## 현재 상태: 스레드 관리 주체에 묶이지 않는다

**지금 있는 것들은 어느 스레드에서 불려도 동작한다.** 스레드를 만들거나 관리하는 코드가
라이브러리에 아예 없고, 생 `std::thread`에서 정상 동작하는 것이 테스트로 검증되어 있다.
전역 변수와 `thread_local` 전역도 하나도 없다. 지향은 프레임워크보다 **부품 모음**
쪽이다.

그래서 새 코드를 쓸 때 이 방식을 **먼저 검토한다.**

* 스레드별 상태가 필요하면 **함수 지역 `thread_local`로 스스로 초기화**한다. 등록·정리가 필요하면 그 객체의 생성자·소멸자가 처리한다(`SendBuffer`의 계측 블록이 이 패턴이다).
* 외부에서 배정받는 스레드 식별자에 의존하지 않는다. 과거 `MyThreadID`가 그런 값이었고 `InitTLS()`를 거쳐야만 유효했다.
* "쓰기 전에 초기화 훅을 불러야 한다"는 계약을 만들지 않는다.
* 전역 변수를 되살리기 전에 한 번 더 생각한다. 전역은 곧 "누가 언제 초기화하는가"를 만든다.

**단, 이게 절대 규칙은 아니다.** 스레드 관리 주체에 묶여야만 성립하는 컴포넌트가
실제로 필요해지면 그때 판단하면 된다. 다만 그건 **명시적으로 내려야 할 결정**이므로,
슬쩍 도입하지 말고 `design/OPEN_QUESTIONS.md`에 질문을 열 것.

## 빌드

```sh
cmake -S . -B build                    # 최초 configure 시 FetchContent가 moodycamel/concurrentqueue v1.0.4를 받아온다
cmake --build build --config Debug     # 멀티 컨피그 생성기(Visual Studio)라 --config가 필요하다
ctest --test-dir build -C Debug --output-on-failure
```

테스트는 `SendBuffer`와 `Actor` 둘이다. 하나만 돌리려면 `ctest ... -R Actor`, 또는 실행
파일을 직접 실행한다(`build/tests/Debug/ActorTest.exe`). 실행 파일이 요약을 출력하므로
직접 실행하는 쪽이 정보가 많다. Actor 쪽은 경합을 노린 반복이 많아 약 6초 걸린다.

테스트는 이 레포를 **단독으로 빌드할 때만** 켜진다. 다른 프로젝트가 `add_subdirectory`로
가져다 쓸 때는 꺼진다(`MYUTILS_BUILD_TESTS`).

lint 단계와 install 규칙은 없다.

## 문서 구조

같은 내용을 여러 곳에 중복해서 쓰지 않는다. 기준은 **언제 읽어야 하는가**다.

| 내용                                   | 위치                         |
| ------------------------------------ | -------------------------- |
| 이 줄을 바꾸면 무엇이 깨지는가                    | 코드 주석                      |
| **여러 파일에 걸친 시퀀스·상태표·락 규칙**           | `design/<컴포넌트>.md`         |
| 지금 여기서 작업하려면 알아야 할 것 (현재 상태, 규칙, 함정) | **이 파일**                   |
| 정형화된 AI 개발 workflow의 단계와 실행 규칙       | `workflow.md`              |
| **아직 정하지 못한 것**                      | `design/OPEN_QUESTIONS.md` |
| 완료 이력, 착수 가능한 TODO                   | `progress.md`              |

**이 레포는 "왜 그렇게 정했는가"를 보관하지 않는다.** 기록하는 것은 **지금 무엇이
어떻게 동작하는가**와 **무엇을 건드리면 깨지는가**뿐이다. 검토했다 버린 대안, 당시의
판단 근거, 미래의 재검토 조건은 남기지 않는다.

그래서 변경할 때 **읽어야 할 문서가 적다.** 대신 현재 동작을 바꿀 때는 그 동작을
서술한 주석과 문서를 같은 변경에서 함께 고쳐야 한다 — 뒤에서 맞춰줄 이력 문서가 없다.

설계 또는 구현 작업을 시작하기 전에는 다음 순서로 문맥을 확인한다.

1. `design/OPEN_QUESTIONS.md`에서 아직 결정되지 않은 문제가 있는지 확인한다.
2. `progress.md`에서 현재 결정 대기 항목과 바로 착수 가능한 작업을 확인한다.
3. 수정 대상 컴포넌트의 코드 주석과 `design/*.md`에서 현재 실행 규약을 확인한다.

컴포넌트 스펙(`design/actor-runtime.md`)에는 **"현재 동작 서술"을 통째로 옮기지 않는다.**
그게 가장 잘 낡는다. 한 파일 안에서 설명되는 규칙은 코드 주석에 두고, 파일을 넘나드는
시퀀스만 스펙에 둔다. 스펙의 각 invariant는 자신을 검증하는 테스트 이름을 달아
드리프트를 막는다.

`design/OPEN_QUESTIONS.md`는 여러 세션이 같은 문서를 보고 이어서 논의하는 공간이다.
참여 규칙(서명·날짜, 측정 대기 항목에 의견 쌓지 않기)은 파일 상단을 따른다.
결론이 나면 그 결정을 **코드 주석이나 `design/*.md`의 현재 동작 서술로 직접 반영하고**
질문은 지운다. `progress.md` TODO와의 경계는 "오늘 바로 착수할 수 있는가"다 — 먼저
골라야 할 게 있으면 OPEN_QUESTIONS 쪽이다.

## 인코딩 (중요)

소스와 헤더는 전부 **BOM 없는 UTF-8**이고 주석은 한국어다. MSVC는 기본적으로 시스템
코드 페이지(한국어 환경이면 949)로 읽기 때문에, 한글 주석의 마지막 바이트가 CP949
lead byte로 오해되어 **줄바꿈까지 삼키고 다음 줄이 주석에 먹히는 일이 실제로 발생한다**
(예: "양" = `EC 96 91` → `91`이 lead byte가 되어 `\n`을 소비). 증상이 "멀쩡히 있는 멤버를
찾을 수 없다"는 C2039 에러로 나타나므로 원인을 찾기 어렵다.

`CMakeLists.txt`가 `target_compile_options(MyUtils PUBLIC /utf-8)`로 이를 막고 있다.
**이 옵션을 지우지 말 것.** 공개 헤더에도 한글 주석이 있어 소비자에게도 전파해야 하므로
PUBLIC이다.

## 헤더 작성 규칙

* **미리 컴파일된 헤더(PCH)가 없다.** `src/pch.h`는 2026-09-15에 제거했다. 실제로 쓰는 `.cpp`가 하나도 남지 않았는데도 강제 삽입되어 `<windows.h>`와 `using namespace std;`를 모든 TU에 끌고 들어오고 있었다. **다시 도입하지 말 것** — 필요한 것은 각 파일이 직접 include한다.
* `CRASH` / `ASSERT_CRASH` / `DEBUG_BREAK`는 `include/MyUtils/Assert.h`에 있다. 쓰려면 직접 include한다.
* `ASSERT_CRASH`에는 `NDEBUG` 가드가 없어 **릴리즈에서도 죽는다.** 계약 위반(프로그래밍 오류)에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
* 모든 파일이 자립적이어야 한다: `#pragma once`, 필요한 표준 헤더 명시, 이름은 전부 `std::`로 한정. 과거에 `#pragma once` 누락과 `std::` 누락으로 각각 별도의 수정 커밋이 필요했던 지점이다.
* 새 소스 파일을 추가하면 `CMakeLists.txt`의 `add_library` 목록에 직접 넣어야 한다.
* 주석과 커밋 메시지는 한국어다.

## 아키텍처

### 계측

각 컴포넌트가 자기 계측을 갖는다. 조회는 `MyUtils::Network::SnapshotStats()`,
`MyUtils::Actors::SnapshotStats()`, `LiveChunkCount()`, `LiveChunkBytes()`,
`PoolIdleCountApprox()`다. 두 테스트 실행 파일이 종료 시 요약을 출력한다.

* **전부 항상 켜져 있고 끄는 스위치가 없다.** 지금 있는 지표의 비용이 증가 연산이거나 chunk·배치당 clock 1회라, 메시지당으로 환산하면 묻히기 때문이다.
* **새 지표를 추가할 때 기준은 "메시지당 clock을 읽는가"다.** 그렇다면 넣지 말 것 — 꼭 필요하면 켜고 끄는 수단을 함께 설계해야 한다. 지금은 그런 지표가 없어서 스위치도 없다.
* 구현은 자가 등록 `thread_local` 블록이다. 생성자가 레지스트리에 등록하고 소멸자가 누적값을 접어 넣으므로, 어느 스레드에서 쓰이든 동작하고 스레드가 죽어도 값을 잃지 않는다.

### Actor Runtime (`Actor.h` / `ActorRuntime.cpp`)

**실행 규약은 `design/actor-runtime.md`에 있다.** 상태 전이표, 두 가지 lost wakeup 프로토콜, 락 규칙, Finalize·Shutdown 시퀀스, invariant ↔ test 표가 거기 있다. 여기에는 손대기 전에 반드시 알아야 할 것만 적는다.

* **`Actor::Handle()`을 mailbox 락을 쥔 채 호출하지 말 것.** self-send / self-stop / handler 안에서의 `Spawn`이 전부 여기에 의존한다. **"배치 처리니까 락을 한 번만 잡자"는 최적화가 자연스럽게 들어올 자리다.** 어기면 `SelfSendFromHandler` / `SelfStopFromHandler` / `SpawnFromHandler` 테스트가 타임아웃한다.
* **drain 루프는 pop 전에 mailbox 락 안에서 state가 `RUNNING`인지 확인한다.** `Stop`은 state만 바꿀 뿐 worker는 여전히 루프 안이라, 이 검사가 없으면 Stop 이후에도 pending을 budget 한도까지 처리한다. 검사를 `Handle()` 반환 직후로 옮기면 검사와 pop 사이에 창이 생겨 경계가 무너진다 — **락 안이어야 한다.** `SelfStopFromHandler`가 잡는다.
* **`Stop()`의 이전 상태는 CAS 루프로 포착할 것.** `load` 후 `store`로 쓰면 그 사이에 producer가 `IDLE → SCHEDULED`로 바꿔(이 전이는 mailbox 락 밖에서 일어난다) Finalize 주체 판정이 틀린다.
* **runnable을 만드는 경로는 `EnqueueRunnable` 하나뿐이다.** 이 함수가 "local deque냐 injection queue냐"를 고르고 필요한 notify까지 한다 — **한 곳에 모으는 게 아니라 진입점을 우회하지 않는 것**이 규칙이다. 우회하면 그 경로에서만 worker가 깨지 않는다.
* **local deque로 보낼지 판정할 때 worker index만 보지 말 것.** `thread_local`에 담긴 `ActorRuntime*`도 함께 확인한다. `ActorSystem`이 둘 이상이면 시스템 A의 worker가 만든 B의 runnable이 A의 deque로 샌다. `CrossRuntimeIsolation`이 잡는다.
* **유휴 worker 수(`_idleCount`)는 sleep 락 안에서 증감할 것.** 락 밖에서 세면 producer가 0을 읽고 notify를 건너뛴 직후 worker가 잠드는 창이 생긴다.
* **`Send()`가 `true`여도 전달은 보장되지 않는다.** accept되었다는 뜻이며, 이후 `Stop`/`Shutdown`이 버릴 수 있다.
* `Stop()`은 graceful stop이 아니다. pending 메시지를 버린다.
* Registry를 순회하며 `Finalize`하지 말 것. `Finalize`가 스스로를 unregister하므로 스냅샷을 만든 뒤 락 밖에서 처리한다.

**스케줄러는 global injection queue(MPMC) + worker-local deque + work stealing이다.** 구조와 정책은 `design/actor-runtime.md`의 "스케줄러 구조" 절에 있다. 두 정책 상수는 **측정값이 아니라 baseline**이므로 임의로 바꾸지 말고 `SnapshotStats()`로 문제를 먼저 확인할 것.

* `INJECTION_POLL_INTERVAL`(61) — 몇 번에 한 번 injection을 local보다 먼저 보는가. 없으면 local deque가 차 있는 동안 injection이 굶는다.
* `DEFAULT_MESSAGE_BUDGET`(32) — **위 값과 상호작용한다.** 한 번의 local 처리가 최대 32건이라 injection 확인 간격은 최악의 경우 메시지 1,952건이다. 한쪽을 바꾸면 다른 쪽의 의미도 바뀐다.

**두 큐는 notify 정책이 다르고, 그게 의도다.** injection은 항상 깨우고(correctness — 빠뜨리면 아무도 안 깬다), local은 유휴 worker가 있을 때만 깨운다(heuristic — 빠뜨려도 owner가 결국 처리한다). **"일관성 있게 통일하자"고 합치지 말 것.**

### 송신 버퍼 (`SendBuffer.h` / `SendBuffer.cpp`)

thread-local bump allocator + refcounted arena 구조다.

```text
Reserve(capacity) → SendBuffer (이동 전용, mutable)
                  → Commit(actualSize) → SendView (복사 가능, immutable)
```

**용도가 한정된 allocator다.** 실시간 통신의 빈번한 I/O hot path에서 작은~중간 크기 송신 버퍼를 빠르게 할당하는 것이 목적이고, **느린 peer가 `SendView`를 오래 붙잡아 생기는 memory pinning 최소화는 목표가 아니다.** 그래서 pinning을 줄이려고 chunk 크기를 낮추는 변경을 넣지 말 것 — 필요하면 상위 session 정책에서 대응한다. 자세한 것은 `SendBuffer.h` 상단.

* **`SendBufferManager`의 Current Chunk 참조를 없애지 말 것.** 이 참조가 있어야 "refcount == 0"이 "아무도 이 chunk를 모른다"와 같은 뜻이 된다. 없으면 마지막 I/O가 끝나는 순간 사용 중인 chunk가 pool로 반환되어 두 worker가 같은 chunk에 동시에 bump allocation한다.
* **chunk 소유권 이전은 반드시 ChunkPool을 경유할 것.** `_offset`이 비동기화 상태로 안전한 근거가 pool 큐의 happens-before다. worker 사이에서 chunk를 직접 넘기는 최적화를 넣는 순간 깨진다.
* **`Reserve` 반환값을 반드시 확인할 것.** 크기 0 또는 `MAX_SEND_BUFFER_SIZE` 초과면 무효 버퍼가 돌아온다. 확인 없이 쓰면 `WritableData()`/`Commit()`에서 죽는다.
* tail reclaim은 LIFO 조건에서만 동작한다. 뒤에 다른 할당이 끼면 회수되지 않는 게 **정상**이다.
* **`Reserve`는 정렬 올림을 하지 않는다.** byte serialization 전용 저장소이며, placement new로 typed object를 만드는 용도가 아니다.
* 계측(`SnapshotStats`)은 살아 있는 thread의 카운터를 잠금 없이 읽는 의도된 benign race다. 정확한 값이 필요하면 대상 thread를 join한 뒤 읽는다.

정책 상수는 전부 튜닝 값이며 correctness와 무관하다. 현재 값(chunk 64 KB, large 임계 16 KB, pool 유휴 64)은 **최적값이 아니라 baseline**이다. 임의로 바꾸지 말고 `SnapshotStats()`로 문제를 먼저 확인할 것.

특히 **`LARGE_ALLOCATION_THRESHOLD`를 `DEFAULT_CHUNK_CAPACITY`와 같게 맞추지 말 것.** 전자는 "current chunk를 교체시킬 만큼 큰 요청인가"를 판정하는 정책값이고 후자는 저장 공간 크기라, 의미가 다르다. 같게 두면 20~30 KB 요청 하나가 64 KB chunk를 갈아치우며 남은 공간을 통째로 버린다.
