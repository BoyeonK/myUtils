# CLAUDE.md

`MyUtils`는 게임 서버류 애플리케이션의 런타임 기반 요소를 제공하는 C++17 정적 라이브러리(CMake 타겟 `MyUtils::MyUtils`)다. 실행 파일은 없다. 다른 프로젝트에서 `add_subdirectory` / `FetchContent`로 가져와 `target_link_libraries(... MyUtils::MyUtils)`로 링크해 쓴다.

## 컴포넌트

수정 전에 해당 문서를 읽는다. 동작을 바꿨으면 같은 변경에서 문서도 고친다.

| 컴포넌트 | 코드 | 문서 |
| --- | --- | --- |
| 송신 버퍼 | `include/MyUtils/SendBuffer.h`, `src/SendBuffer.cpp` | [`design/send-buffer.md`](design/send-buffer.md) |
| Actor Runtime | `include/MyUtils/Actor.h`, `src/ActorRuntime.cpp` | [`design/actor-runtime.md`](design/actor-runtime.md) |

상태 전이표, 락 규칙, 수명·소유권 관계, 정책 상수의 의미, 계측 지표 목록은 전부 위 문서에 있다.

## 작업 절차

시작 전에 다음을 확인한다.

1. `design/OPEN_QUESTIONS.md` — 아직 결정되지 않은 문제
2. `progress.md` — 결정 대기 항목, 바로 착수 가능한 작업
3. 대상 컴포넌트의 코드 주석과 `design/*.md` — 현재 실행 규약

요청이 `워크플로우 시작:`으로 시작하는 경우에만 다른 일을 하기 전에 `workflow.md`를 읽고 그 절차와 상태 전이 규칙을 따른다. 세션을 시작했다는 사실만으로 진입하지 않는다.

수정 후에는 관련 테스트를 실행한다. 실행하지 않았거나 실행할 수 없었으면 그 사실을 명시한다.

## 빌드

```sh
cmake -S . -B build                    # 최초 configure 시 FetchContent가 moodycamel/concurrentqueue v1.0.4를 받아온다
cmake --build build --config Debug     # 멀티 컨피그 생성기에서는 --config가 필요하다
ctest --test-dir build -C Debug --output-on-failure
```

테스트 실행 파일은 종료 시 계측 요약을 출력하므로 직접 실행하는 쪽이 정보가 많다(`build/tests/Debug/ActorTest.exe`). Actor 쪽은 경합을 노린 반복이 많아 약 6초 걸린다. 테스트는 이 레포를 단독으로 빌드할 때만 켜진다(`MYUTILS_BUILD_TESTS`). lint 단계와 install 규칙은 없다.

## 금지 사항과 함정

* **MSVC `/source-charset:utf-8` 컴파일 옵션을 지우지 말 것.** 소스와 공개 헤더는 BOM 없는 UTF-8이다.
* **공개 헤더에 접두사 없는 매크로를 만들지 말 것.** 소비자 쪽 매크로와 충돌한다. 공개 매크로는 `MYUTILS_ASSERT` 하나뿐이다.
* 모든 파일이 자립적이어야 한다 — `#pragma once`, 필요한 표준 헤더 명시, 이름은 전부 `std::`로 한정. **강제 삽입되는 헤더에 기대지 말 것.** `#pragma once` 누락과 `std::` 누락으로 각각 별도의 수정 커밋이 필요했던 지점이다.

## 설계 제약

* **스레드 관리 주체에 묶이지 않는다.** 스레드를 만들거나 관리하는 코드가 없고, 전역 변수와 전역 `thread_local`도 없다. 스레드별 상태는 함수 지역 `thread_local`로 스스로 초기화하고, 외부에서 배정받는 스레드 식별자나 초기화 훅을 요구하는 계약을 만들지 않는다. 이 제약을 깨야 한다면 `design/OPEN_QUESTIONS.md`에 질문을 먼저 연다. 상세: [`design/threading.md`](design/threading.md)
* **어느 컴파일러도 특별 취급하지 않는다.** 소스는 표준 C++17 + `concurrentqueue`뿐이고 플랫폼 헤더를 쓰지 않는다. 컴파일러별 차이는 두 곳에만 둔다 — `CMakeLists.txt`의 제너레이터 표현식과 `src/Assert.cpp`의 중단 수단 분기. 세 번째 자리를 만들기 전에 `design/OPEN_QUESTIONS.md`에 질문을 연다.
* **계약 위반은 그 자리에서 죽인다.** `MYUTILS_ASSERT`는 `NDEBUG` 가드가 없어 릴리즈에서도 동작한다. 복구 가능한 실패는 반환값으로 알린다.
* 문서와 주석의 정책 상수는 측정에서 나온 최적값이 아니라 **baseline**이다.
* 구현 수단은 **검증된 라이브러리 → mutex 기반 → 직접 작성한 lock-free** 순으로 검토한다. 세 번째는 측정 근거가 있을 때만 연다.
* 계측은 전부 항상 켜져 있고 끄는 스위치가 없다. 새 지표 추가 기준: [`design/instrumentation.md`](design/instrumentation.md)

기존 설계와 코드가 어긋나면 임의로 한쪽을 맞추지 말고 먼저 차이를 보고한다.

---

문서 작성 규약(무엇을 어디에 쓰는가)은 [`design/DOC_RULES.md`](design/DOC_RULES.md)를 따른다.
