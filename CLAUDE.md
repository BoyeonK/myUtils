# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 개요

`MyUtils`는 게임 서버류 애플리케이션의 런타임 기반 요소를 제공하는 C++17 정적 라이브러리(CMake 타겟 `MyUtils::MyUtils`)다. 실행 파일은 없고, 다른 프로젝트에서 `add_subdirectory` / `FetchContent`로 가져와 `target_link_libraries(... MyUtils::MyUtils)`로 링크해 쓴다.

현재 구성 요소는 셋이다.

- **송신 버퍼** (`SendBuffer.h`) — thread-local bump allocator + refcounted chunk. 완성된 컴포넌트다.
- **Actor Runtime** (`Actor.h`) — MPSC mailbox + atomic 상태 머신 + worker pool. 실행 규약은 `design/actor-runtime.md`, 근거는 ADR-0010~0012.
- **스레드 런처** (`Thread.h`) — `ThreadManager`의 launch/join이 전부. **현재 라이브러리의 어떤 것도 이걸 거쳤는지 묻지 않는다**(ADR-0009). 편의 유틸리티로 남아 있다.

액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러, 그리고 전역 변수 일체를 **2026-09-15에 전부 제거했다.** 전면 재작성 대상이었고, 그 위에 무언가를 쌓는 것보다 비우고 다시 세우는 편이 낫다는 판단이었다. 필요하면 git 히스토리(`2ce7019` 이전)에서 참고할 수 있다.

## 현재 상태: 스레드 관리 주체에 묶이지 않는다

**지금 있는 것들은 어느 스레드에서 불려도 동작한다.** `SendBuffer`는 `ThreadManager`를
전혀 거치지 않고, 생 `std::thread`에서 정상 동작하는 것이 검증되어 있다. 전역 변수와
`thread_local` 전역도 하나도 없다. 지향은 프레임워크보다 **부품 모음** 쪽이다(ADR-0009).

그래서 새 코드를 쓸 때 이 방식을 **먼저 검토한다.**

- 스레드별 상태가 필요하면 **함수 지역 `thread_local`로 스스로 초기화**한다. 등록·정리가 필요하면 그 객체의 생성자·소멸자가 처리한다(`SendBuffer`의 계측 블록이 이 패턴이다).
- 외부에서 배정받는 스레드 식별자에 의존하지 않는다. 과거 `MyThreadID`가 그런 값이었고 `InitTLS()`를 거쳐야만 유효했다.
- "쓰기 전에 초기화 훅을 불러야 한다"는 계약을 만들지 않는다.
- 전역 변수를 되살리기 전에 한 번 더 생각한다. 전역은 곧 "누가 언제 초기화하는가"를 만든다.

**단, 이게 절대 규칙은 아니다.** 스레드 관리 주체에 묶여야만 성립하는 컴포넌트가
실제로 필요해지면 그때 판단하면 된다 — ADR-0009는 그런 상황을 미리 막아둔 문서가
아니다. 다만 그건 **명시적으로 내려야 할 결정**이므로, 슬쩍 도입하지 말고
`design/OPEN_QUESTIONS.md`에 질문을 열 것.

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

| 내용 | 위치 |
|---|---|
| 이 줄을 바꾸면 무엇이 깨지는가 | 코드 주석 |
| **여러 파일에 걸친 시퀀스·상태표·락 규칙** | `design/<컴포넌트>.md` |
| 지금 여기서 작업하려면 알아야 할 것 (현재 상태, 규칙, 함정) | **이 파일** |
| 왜 이 모양으로 정했고 무엇을 검토했다 버렸는가 | `design/adr/` |
| **아직 정하지 못한 것** | `design/OPEN_QUESTIONS.md` |
| 완료 이력, 착수 가능한 TODO | `progress.md` |

컴포넌트 스펙(`design/actor-runtime.md`)에는 **"현재 동작 서술"을 통째로 옮기지 않는다.**
그게 가장 잘 낡는다. 한 파일 안에서 설명되는 규칙은 코드 주석에 두고, 파일을 넘나드는
시퀀스만 스펙에 둔다. 스펙의 각 invariant는 자신을 검증하는 테스트 이름을 달아
드리프트를 막는다.

ADR은 **그 시점의 결정을 박제한 기록**이라 고치지 않는다. 결정이 바뀌면 새 ADR을 쓰고 기존 문서의 `Status`를 `Superseded by ADR-00XX`로 바꾼다. 설계 근거를 이 파일에 옮겨 적지 말 것 — 그러면 CLAUDE.md가 낡기 시작한다.

**설계 판단이 필요한 작업을 시작하기 전에 `design/OPEN_QUESTIONS.md`를 먼저 볼 것.** 여러 세션이 같은 문서를 보고 이어서 논의하도록 만든 파일이라, 참여 규칙(서명·날짜, 측정 대기 항목에 의견 쌓지 않기, ADR 재논쟁 금지)이 파일 상단에 적혀 있다. `progress.md` TODO와의 경계는 "오늘 바로 착수할 수 있는가"다 — 먼저 골라야 할 게 있으면 OPEN_QUESTIONS 쪽이다.

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

- **미리 컴파일된 헤더(PCH)가 없다.** `src/pch.h`는 2026-09-15에 제거했다. 실제로 쓰는 `.cpp`가 하나도 남지 않았는데도 강제 삽입되어 `<windows.h>`와 `using namespace std;`를 모든 TU에 끌고 들어오고 있었다. **다시 도입하지 말 것** — 필요한 것은 각 파일이 직접 include한다.
- `CRASH` / `ASSERT_CRASH` / `DEBUG_BREAK`는 `include/MyUtils/Assert.h`에 있다. 쓰려면 직접 include한다.
- `ASSERT_CRASH`에는 `NDEBUG` 가드가 없어 **릴리즈에서도 죽는다.** 계약 위반(프로그래밍 오류)에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- 모든 파일이 자립적이어야 한다: `#pragma once`, 필요한 표준 헤더 명시, 이름은 전부 `std::`로 한정. 과거에 `#pragma once` 누락과 `std::` 누락으로 각각 별도의 수정 커밋이 필요했던 지점이다.
- 새 소스 파일을 추가하면 `CMakeLists.txt`의 `add_library` 목록에 직접 넣어야 한다.
- 주석과 커밋 메시지는 한국어다.

## 아키텍처

### Actor Runtime (`Actor.h` / `ActorRuntime.cpp`)

**실행 규약은 `design/actor-runtime.md`에 있다.** 상태 전이표, 두 가지 lost wakeup 프로토콜, 락 규칙, Finalize·Shutdown 시퀀스, invariant ↔ test 표가 거기 있다. 여기에는 손대기 전에 반드시 알아야 할 것만 적는다.

- **`Actor::Handle()`을 mailbox 락을 쥔 채 호출하지 말 것.** self-send / self-stop / handler 안에서의 `Spawn`이 전부 여기에 의존한다. **"배치 처리니까 락을 한 번만 잡자"는 최적화가 자연스럽게 들어올 자리다.** 어기면 `SelfSendFromHandler` / `SelfStopFromHandler` / `SpawnFromHandler` 테스트가 타임아웃한다.
- **`Stop()`의 이전 상태는 CAS 루프로 포착할 것.** `load` 후 `store`로 쓰면 그 사이에 producer가 `IDLE → SCHEDULED`로 바꿔(이 전이는 mailbox 락 밖에서 일어난다) Finalize 주체 판정이 틀린다.
- **runnable을 만드는 경로는 `EnqueueRunnable` 하나뿐이다.** 큐 삽입과 worker 깨우기가 거기서 묶인다. work stealing을 넣을 때 이걸 우회하면 그 경로에서만 worker가 깨지 않는다.
- **`Send()`가 `true`여도 전달은 보장되지 않는다.** accept되었다는 뜻이며, 이후 `Stop`/`Shutdown`이 버릴 수 있다.
- `Stop()`은 graceful stop이 아니다. pending 메시지를 버린다.
- Registry를 순회하며 `Finalize`하지 말 것. `Finalize`가 스스로를 unregister하므로 스냅샷을 만든 뒤 락 밖에서 처리한다.

work stealing은 아직 없다. 단일 global runnable queue만 쓴다.

### 스레드 런처 (`Thread.h` / `Thread.cpp`)

`ThreadManager`는 `std::thread`를 벡터에 모아 join하는 것이 전부다. TLS 초기화 훅(`InitTLS`/`DestroyTLS`)이 있었지만 초기화할 대상이 전부 죽은 전역이라 2026-09-15에 함께 제거했다.

여기에 스레드별 초기화를 다시 넣으려 한다면, 그 순간 "그걸 거친 스레드여야 한다"는 전제가 생긴다는 점을 의식할 것. 그게 필요한 상황이면 그때 결정하면 되지만(ADR-0009), 무심코 들어가기 쉬운 자리다.

`SendBuffer`가 `ThreadManager`를 전혀 거치지 않는 것이 현재의 예다 — chunk 수명은 refcount가, current chunk는 함수 지역 `thread_local SendBufferManager`가 관리하며 그 소멸자가 스레드 종료 시 알아서 반납한다. 생 `std::thread`에서도 정상 동작한다(검증 완료).

### 송신 버퍼 (`SendBuffer.h` / `SendBuffer.cpp`)

thread-local bump allocator + refcounted arena 구조다.

```
Reserve(capacity) → SendBuffer (이동 전용, mutable)
                  → Commit(actualSize) → SendView (복사 가능, immutable)
```

**왜 이 모양인지, 무엇을 검토했다 버렸는지는 `design/adr/`에 있다(0001~0008).** 여기에는 작업 시 반드시 지켜야 할 것만 적는다.

- **`SendBufferManager`의 Current Chunk 참조를 없애지 말 것.** 이 참조가 있어야 "refcount == 0"이 "아무도 이 chunk를 모른다"와 같은 뜻이 된다. 없으면 마지막 I/O가 끝나는 순간 사용 중인 chunk가 pool로 반환되어 두 worker가 같은 chunk에 동시에 bump allocation한다.
- **chunk 소유권 이전은 반드시 ChunkPool을 경유할 것.** `_offset`이 비동기화 상태로 안전한 근거가 pool 큐의 happens-before다. worker 사이에서 chunk를 직접 넘기는 최적화를 넣는 순간 깨진다.
- **`Reserve` 반환값을 반드시 확인할 것.** 크기 0 또는 `MAX_SEND_BUFFER_SIZE` 초과면 무효 버퍼가 돌아온다. 확인 없이 쓰면 `WritableData()`/`Commit()`에서 죽는다.
- tail reclaim은 LIFO 조건에서만 동작한다. 뒤에 다른 할당이 끼면 회수되지 않는 게 **정상**이다.
- 계측(`SnapshotStats`)은 살아 있는 thread의 카운터를 잠금 없이 읽는 의도된 benign race다. 정확한 값이 필요하면 대상 thread를 join한 뒤 읽는다.

정책 상수는 전부 튜닝 값이며 correctness와 무관하다. 현재 값(chunk 64 KB, large 임계 16 KB, pool 유휴 64)은 **최적값이 아니라 합의된 baseline policy**이고, 근거와 재검토 조건은 ADR-0008에 있다. 임의로 바꾸지 말고 `SnapshotStats()`로 문제를 먼저 확인할 것.

특히 **`LARGE_ALLOCATION_THRESHOLD`를 `DEFAULT_CHUNK_CAPACITY`와 같게 맞추지 말 것.** 전자는 "current chunk를 교체시킬 만큼 큰 요청인가"를 판정하는 정책값이고 후자는 저장 공간 크기라, 의미가 다르다. 같게 두면 20~30 KB 요청 하나가 64 KB chunk를 갈아치우며 남은 공간을 통째로 버린다.
