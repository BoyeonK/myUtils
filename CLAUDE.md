# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 개요

`MyUtils`는 게임 서버류 애플리케이션의 런타임 기반 요소를 제공하는 C++17 정적 라이브러리(CMake 타겟 `MyUtils::MyUtils`)다. 실행 파일은 없고, 다른 프로젝트에서 `add_subdirectory` / `FetchContent`로 가져와 `target_link_libraries(... MyUtils::MyUtils)`로 링크해 쓴다.

현재 구성 요소는 두 가지뿐이다.

- **송신 버퍼** (`SendBuffer.h`) — thread-local bump allocator + refcounted chunk
- **스레드 런처** (`Thread.h`) — `ThreadManager`의 launch/join과 TLS 초기화

액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러는 **2026-09-15에 전부 제거했다.** 전면 재작성 대상이었고, 그 위에 무언가를 쌓는 것보다 비우고 다시 세우는 편이 낫다는 판단이었다. 필요하면 git 히스토리(`2ce7019` 이전)에서 참고할 수 있다.

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

## 문서 구조

세 곳에 같은 내용을 중복해서 쓰지 않는다. 기준은 **언제 읽어야 하는가**다.

| 내용 | 위치 |
|---|---|
| 이 줄을 바꾸면 무엇이 깨지는가 | 코드 주석 |
| 지금 여기서 작업하려면 알아야 할 것 (현재 상태, 규칙, 함정) | **이 파일** |
| 왜 이 모양으로 정했고 무엇을 검토했다 버렸는가 | `design/adr/` |
| **아직 정하지 못한 것** | `design/OPEN_QUESTIONS.md` |
| 완료 이력, 착수 가능한 TODO | `progress.md` |

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

- `src/pch.h`는 **PRIVATE** 미리 컴파일된 헤더다. `using namespace std;`를 하고, `<windows.h>`를 끌어오며(`NOMINMAX`가 없어서 `min`/`max` 매크로가 살아 있다), `MyUtils/Assert.h`를 include한다. `GlobalVariables.cpp`와 `Thread.cpp`가 아직 이 PCH에 암묵적으로 의존한다 — `mt19937`을 네임스페이스 없이 쓴다.
- **새 코드는 PCH에 기대지 말 것.** `CRASH` / `ASSERT_CRASH` / `DEBUG_BREAK`는 `include/MyUtils/Assert.h`에 있으니 직접 include한다. PCH를 통해 쓰면 `windows.h`와 `using namespace std;`까지 그 TU에 딸려 들어온다. `src/SendBuffer.cpp`가 이 방식을 따르며, PCH 없이도 단독으로 컴파일된다.
- `ASSERT_CRASH`에는 `NDEBUG` 가드가 없어 **릴리즈에서도 죽는다.** 계약 위반(프로그래밍 오류)에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- 공개 헤더(`include/MyUtils/`)는 PCH에 의존하면 **안 된다.** 반드시 자립적이어야 한다: `#pragma once`, 필요한 표준 헤더 명시, 이름은 전부 `std::`로 한정. 과거에 `#pragma once` 누락과 `std::` 누락으로 각각 별도의 수정 커밋이 필요했던 지점이다.
- 새 소스 파일을 추가하면 `CMakeLists.txt`의 `add_library` 목록에 직접 넣어야 한다.
- 주석과 커밋 메시지는 한국어다.

## 아키텍처

### 전역 상태와 초기화 (`GlobalVariables.h` / `.cpp`)

남은 전역은 `GThreadManager` 하나뿐이다. `GlobalVariables.h`에서 `extern`으로 선언하고, `GlobalVariables.cpp`의 파일 스코프 객체 `CoreGlobal GCoreGlobal`이 정적 초기화 시점에 `new`하고 종료 시 `delete`한다.

스레드별 상태는 `thread_local` 전역으로 `MyThreadID`, `LEndTickCount`, `LRanGen` 셋이다. `ThreadManager::InitTLS()`가 `MyThreadID`를 배정하고 `LRanGen`을 시드하며, `ThreadManager` 생성자(메인 스레드용)와 `ThreadManager::Launch`로 띄운 모든 스레드의 시작 지점에서 호출된다. `MyThreadID`는 단조 증가하며 재사용하지 않는다. 짝이 되는 `DestroyTLS()`는 현재 비어 있다.

송신 버퍼는 전역 변수도, `DestroyTLS` 훅도 쓰지 않는다. chunk 수명은 refcount가, current chunk는 함수 지역 `thread_local SendBufferManager`가 관리하며 그 소멸자가 스레드 종료 시 알아서 반납한다. 그래서 **`SendBuffer`는 `ThreadManager`를 거치지 않은 생 `std::thread`에서도 정상 동작한다**(검증 완료). 이 성질을 유지할 것.

### 송신 버퍼 (`SendBuffer.h` / `SendBuffer.cpp`)

thread-local bump allocator + refcounted arena 구조다.

```
Reserve(capacity) → SendBuffer (이동 전용, mutable)
                  → Commit(actualSize) → SendView (복사 가능, immutable)
```

**왜 이 모양인지, 무엇을 검토했다 버렸는지는 `design/adr/0001`~`0006`에 있다.** 여기에는 작업 시 반드시 지켜야 할 것만 적는다.

- **`SendBufferManager`의 Current Chunk 참조를 없애지 말 것.** 이 참조가 있어야 "refcount == 0"이 "아무도 이 chunk를 모른다"와 같은 뜻이 된다. 없으면 마지막 I/O가 끝나는 순간 사용 중인 chunk가 pool로 반환되어 두 worker가 같은 chunk에 동시에 bump allocation한다.
- **chunk 소유권 이전은 반드시 ChunkPool을 경유할 것.** `_offset`이 비동기화 상태로 안전한 근거가 pool 큐의 happens-before다. worker 사이에서 chunk를 직접 넘기는 최적화를 넣는 순간 깨진다.
- **`Reserve` 반환값을 반드시 확인할 것.** 크기 0 또는 `MAX_SEND_BUFFER_SIZE` 초과면 무효 버퍼가 돌아온다. 확인 없이 쓰면 `WritableData()`/`Commit()`에서 죽는다.
- tail reclaim은 LIFO 조건에서만 동작한다. 뒤에 다른 할당이 끼면 회수되지 않는 게 **정상**이다.
- 계측(`SnapshotStats`)은 살아 있는 thread의 카운터를 잠금 없이 읽는 의도된 benign race다. 정확한 값이 필요하면 대상 thread를 join한 뒤 읽는다.

`DEFAULT_CHUNK_CAPACITY`(16 KB), `LARGE_ALLOCATION_THRESHOLD`, `MAX_SEND_BUFFER_SIZE`, `DEFAULT_POOL_IDLE_LIMIT`은 전부 튜닝 값이며 correctness와 무관하다. `SnapshotStats()`의 이용률(`committedBytes / reservedBytes`), 살아있는 chunk 수, large path 비율, pool 히트율을 보고 조정한다.
