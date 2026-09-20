# Progress

최종 업데이트: 2026-09-20

---

## 완료

### Actor Runtime

- [x] (2026-09-15 #19) `Spawn()`과 `Shutdown()`의 Registry 등록 경합을 수정했다. `Spawn`이 accepting 확인을 통과한 뒤 ACB 할당에서 멈추고, 그 사이 `Shutdown`이 빈 Registry의 snapshot/clear를 끝내면 종료 후 Actor가 등록되어 `LiveActorCount()==1`이고 `Send()==true`가 되는 실행을 결정적으로 재현했다. accepting gate 변경과 Registry 등록을 같은 Registry 락으로 선형화해, 먼저 등록된 Actor는 shutdown snapshot에 반드시 포함되고 늦은 등록은 무효 `ActorRef`로 거부되게 했다. `SpawnConcurrentWithShutdownRejected`가 해당 경합 창을 고정한다.

### 빌드 / 이식성

- [x] (2026-09-20 #1) 컴파일러 의존을 두 곳으로 격리했다. `if(MSVC)`는 이 프로젝트를 configure한 컴파일러로 결정이 굳어 `INTERFACE_COMPILE_OPTIONS`에 그대로 박히므로 소비자 타깃에서 평가되는 제너레이터 표현식(`COMPILE_LANG_AND_ID:CXX,MSVC`)으로 바꿨고, 옵션을 `/utf-8`에서 `/source-charset:utf-8`로 좁혀 실행 문자셋 강제를 인터페이스에서 뺐다. `Assert.h`의 분기는 플랫폼 기반(`__linux__`/`__APPLE__`)이라 Windows + MinGW·clang이 fallback으로 떨어져 `DEBUG_BREAK()`가 no-op이 되고 있었다 — 컴파일러 기반(`__GNUC__`/`__clang__`)으로 교체했다.
- [x] (2026-09-20 #2) 진단 매크로를 `MYUTILS_ASSERT` 하나로 재구성했다. 공개 헤더에 `CRASH`·`DEBUG_BREAK`·`ANALYSIS_ASSUME`이라는 접두사 없는 매크로가 셋이나 있어 소비자와 충돌할 수 있었고, `CRASH(msg)`는 인자를 아무 데도 쓰지 않아 죽어도 정보가 남지 않았다. 중단 수단도 널 역참조(UB)를 만들고 그것을 `ANALYSIS_ASSUME`으로 억제하는 구조여서, 식·파일·줄을 stderr에 남기고 `abort()`하는 `MyUtils::Detail::AssertFailed`(`src/Assert.cpp`)로 바꿨다. 정책(항상 켜짐, 계약 위반 전용)은 그대로이고 호출처 10곳은 이름만 바뀌었다.
- [x] (2026-09-20 #3) 소비자가 `add_subdirectory`로 가져다 쓸 때 공개 헤더가 컴파일되지 않던 것을 고쳤다. 표준 지정이 `set(CMAKE_CXX_STANDARD 17)`뿐이었는데 이건 디렉터리 스코프 변수라 소비자 타깃에 전파되지 않고, 공개 헤더는 중첩 네임스페이스·`std::byte`·`inline constexpr`을 쓴다. 그래서 소비자는 MSVC 기본 표준(C++14)으로 헤더를 컴파일하다 C2429로 실패했다 — 문서가 안내하는 사용법인데 실제로 해본 적이 없어 드러나지 않았다. `target_compile_features(MyUtils PUBLIC cxx_std_17)`로 요구사항을 타깃에 실었다.

### 문서 체계

- [x] (2026-09-20 #0) ADR 14개(`design/adr/`, 1,471줄)와 그것을 전제로 쓰인 문서 규칙을 전부 제거하고, 남길 가치가 있는 4건(allocator 용도 한정, 정렬 미보장, 메시지 전역 FIFO, 구현체 선택 우선순위)만 코드 주석과 `CLAUDE.md`로 이관했다. 이 레포가 보관하는 것을 "현재 동작과 건드리면 깨지는 것"으로 한정해, 변경할 때마다 읽어야 하는 제약 문서를 없애기 위한 것이다. 재검토 조건이 사라지면서 그 조건 전용이던 계측(chunk 고정 시간, `budgetExhaustedCount`, `notifySkippedCount`, steal 지표 둘, 3단 전체)과 `Profiling.h`·`SetProfilingEnabled` API도 함께 걷어냈고, `WorkStealingBalancesLoad`는 steal 카운터 대신 "처리한 worker가 2개 이상"으로 검증하게 바꿨다.
- [x] (2026-09-20 #4) 부트스트랩 문서에서 컴포넌트 세부를 걷어내고 `design/` 아래 주제별 문서로 분리했다(`send-buffer.md`·`threading.md`·`instrumentation.md`·`DOC_RULES.md` 신규). 모든 세션이 항상 읽는 자리라 크기 자체가 비용인데, Actor 쪽 서술은 `actor-runtime.md`와 완전히 중복이었고 SendBuffer 쪽은 헤더 주석과 겹쳐 있었다. `CLAUDE.md`가 218줄에서 57줄이 됐고, 끊어진 참조(`AGENTS.md`·`README.md`·이 파일의 "문서 구조" 절 링크)도 함께 고쳤다.
- [x] (2026-09-20 #5) 금지 사항 6개를 감사해 3개로 줄였다. 성격이 안 맞는 둘(`MYUTILS_ASSERT` 정책, 언어 규약)은 다른 절로 옮기거나 지웠고, PCH 재도입 금지는 나쁜 사례 하나로 메커니즘을 막고 있어 삭제했으며, `add_library` 항목은 실제 위험(`file(GLOB)` 전환)이 걸릴 자리인 `CMakeLists.txt` 주석으로 내렸다. 그 과정에서 한글 문자열 리터럴 14줄을 영어로 바꿨다 — 실행 문자셋을 강제하지 않아 환경마다 출력이 갈리기 때문이다.
- [x] (2026-09-20 #6) 부트스트랩의 두 절(금지 사항·설계 제약)에서 상태 서술을 걷어내고 규칙만 남겼다. "공개 매크로는 하나뿐이다"처럼 `grep` 한 줄로 확인되는 사실은 문서에 캐싱하면 동기화 의무만 생기고, 컴파일러 분기 위치 나열은 실제로 하루 만에 낡았다. 성격이 안 맞던 "설계와 코드가 어긋나면 보고한다"는 `작업 절차`로 옮겼고, 컴포넌트 간 의존을 단방향으로 제한하는 규칙을 새로 넣었다.
- [x] (2026-09-20 #7) 문서가 단언하던 두 가지가 코드와 달라 정정했다. "전역 변수가 하나도 없다"고 되어 있었지만 계측 레지스트리·chunk pool·live chunk 카운터가 있고, "스레드를 만들거나 관리하는 코드가 없다"고 되어 있었지만 `ActorRuntime`이 worker를 만들고 join한다. 실제로 지키는 것은 "초기화 시점을 요구하는 전역을 두지 않는다"와 "중앙 스레드 관리 주체를 요구하지 않는다"이므로 그렇게 다시 썼다.
- [x] (2026-09-20 #8) `design/OPEN_QUESTIONS.md`를 파기하고 참조 11곳을 정리했다. 쓰기 권한이 동등한 여러 에이전트가 같은 문서에 의견을 쌓는다는 전제가 현재 구조(작업 AI + 읽기 전용 리뷰어 + 결정권자)와 맞지 않았고, Q-001~Q-009도 전부 대화로 닫혔다. 유일하게 열려 있던 Q-010은 "Actor 간 공정성을 보장하지 않으며 이 상황은 생산이 소비를 넘어섰다는 신호로 본다"로 결론이 나서 `design/actor-runtime.md`의 알려진 제약으로 옮겼다.

---

## TODO

**오늘 바로 착수할 수 있는 것**만 여기 적는다. 먼저 정해야 할 것이 남은 사안은
여기 적지 말고 보고해서 결정을 받는다.

### 중간

- [ ] **대표 워크로드에서 실측.** 실제 컨텐츠를 붙이고 `SnapshotStats()`를 뽑는다. 정책 상수(chunk 64 KB / large 16 KB / pool 유휴 64 / `INJECTION_POLL_INTERVAL` 61 / `DEFAULT_MESSAGE_BUDGET` 32)가 전부 baseline이라, 이 데이터가 있어야 조정할 근거가 생긴다.

  볼 것 — `committedBytes/reservedBytes`(이용률), `chunkCreateCount/chunkAcquireCount`(pool miss), `largeAllocCount`, `LiveChunkBytes()`, 그리고 배치당 평균 메시지 수와 큐 대기 시간.

- [ ] **CI 빌드.** 범위는 배포 타깃에 달렸다 — Windows 전용이면 MSVC 1종으로 충분하고, 리눅스 서버에 올릴 생각이 있으면 gcc·clang을 더해 3종으로 간다(소스가 표준 C++17 + `concurrentqueue`뿐이라 이식 가능해 **보이지만** MSVC 외로 빌드해 본 적이 없고, 이 머신에 대체 컴파일러가 없어 CI가 유일한 검증 수단이다).

  어느 쪽이든 **소비자 프로브 스텝을 포함한다** — 별도 프로젝트에서 `add_subdirectory`로 가져다 쓰고 공개 헤더를 컴파일해 보는 것. #3이 딱 이 경로를 실행해 본 적이 없어서 생긴 결함이었다.

---

## 알려진 이슈 · 메모

- 2026-09-15에 레거시(액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러, 전역 변수 일체)를 제거하고 Actor Runtime을 백지에서 다시 만들었다. 현재 구성은 **송신 버퍼 + Actor Runtime** 둘이다. 제거 직전 구현은 git 히스토리(`2ce7019` 이전)에 있다.
- **중앙 스레드 관리 주체가 없다.** 스레드가 필요한 컴포넌트가 자기 것을 만들어 소유한다(`ActorRuntime`이 worker를 만들고 join한다). `ThreadManager`는 사용처가 0이었고 `Join()`에 data race까지 있어 2026-09-15에 삭제했다.
- **초기화 시점을 요구하는 전역이 없다.** 계측 레지스트리와 chunk pool 같은 프로세스 전역 상태는 있지만 전부 함수 지역 `static`이거나 상수 초기화라, `InitTLS()` 같은 훅을 먼저 불러야 하는 계약이 없다. 과거의 `MyThreadID`·`LEndTickCount`·`LRanGen`·`GThreadManager`가 그런 종류였고 2026-09-15에 정리했다 — 다시 들이지 말 것. 상세: [`design/threading.md`](design/threading.md)
- `SnapshotStats()`는 살아 있는 스레드의 카운터를 잠금 없이 읽는다. 통계 목적의 의도된 benign race이며, 정확한 값이 필요하면 대상 스레드를 join한 뒤 읽어야 한다.
- **budget 소진 횟수를 더 이상 관측할 수 없다.** `budgetExhaustedCount`를 지웠으므로 `DEFAULT_MESSAGE_BUDGET`(32)이 작은지 판단하려면 `messagesHandled / runCount` 평균이 32에 붙는지로 간접 추정해야 한다. 정확히 필요해지면 그때 다시 넣는다.
- 소스와 헤더는 BOM 없는 UTF-8이고 주석은 한국어다. **MSVC에서 `/source-charset:utf-8`을 빼면 97줄이 CP949 lead byte로 끝나 다음 줄을 주석에 먹힌다.** BOM이 있으면 MSVC가 알아서 UTF-8로 읽으므로 "BOM 없는 UTF-8"이 이 옵션의 전제 조건이다. 근거는 `CMakeLists.txt`의 해당 블록 주석에 있다.
- **실행 문자셋은 강제하지 않는다.** 2026-09-20에 `/utf-8`(소스+실행)에서 `/source-charset:utf-8`(소스만)로 좁혔다. PUBLIC으로 실행 문자셋을 주면 소비자 TU 전체의 리터럴 인코딩까지 이쪽에서 정해버리기 때문이다. 같은 날 한글 문자열 리터럴 14줄(테스트 출력 13, `SendBuffer.h`의 `static_assert` 메시지 1)을 영어로 바꿔 현재는 비ASCII 리터럴이 없다. 한글 리터럴을 다시 넣으면 실행 환경의 코드 페이지에 따라 출력이 갈린다.
- **문서에 적어둔 사용법은 실제로 한 번 실행해 볼 것.** `add_subdirectory`로 가져다 쓰는 경로가 README에 적혀 있는데 아무도 해본 적이 없어, 소비자가 공개 헤더를 컴파일조차 못 하는 상태가 오래 유지됐다(2026-09-20 #3). 빌드·테스트가 전부 통과해도 그건 "이 레포를 단독으로 빌드할 때"만 검증한 것이다.
- **문서를 지울 때는 끊어진 링크뿐 아니라 "그 문서가 유일한 출처인 정보"가 있는지 먼저 확인할 것.** 요구사항 원문을 지우면서 스케줄러 목표 구조를 통째로 잃은 전례가 있다(2026-09-15, `8d13fd1`에서 복원).
- **미커밋 변경이 있는 파일에 `git checkout <파일>`을 쓰지 말 것.** HEAD로 되돌아가 그 파일의 미커밋 작업이 전부 사라진다. 2026-09-15에 2차 스케줄러 구현을 이렇게 날렸다(재적용으로 복구). 코드를 임시로 바꿔 실험할 때는 먼저 사본을 떠두고 그것으로 되돌린다.
- **PowerShell `Set-Content`/`Out-File`로 소스 파일을 쓰지 말 것.** 한글 주석이 깨진다(2026-09-15에 `ActorRuntime.cpp`가 이걸로 깨졌다). 파일을 통째로 바꿔야 하면 Python 바이너리 모드나 `Copy-Item`처럼 바이트를 보존하는 수단을 쓴다.
- **어떤 논증이 일부 경로에만 적용되어 있지 않은지 확인할 것.** "Stop은 state만 바꿀 뿐 worker는 여전히 drain 루프 안이다"가 예외 경로에만 적혀 있고 일반 `Stop` 경로에는 빠져 있었으며, 구현도 그대로 빠뜨렸다(2026-09-15).
- 어디에 무엇을 쓰는지는 [`design/DOC_RULES.md`](design/DOC_RULES.md)에 있다. 같은 내용을 두 곳에 쓰지 말 것.
- **이 레포는 "왜 그렇게 정했는가"를 보관하지 않는다.** 2026-09-20에 ADR 14개를 제거했다. 기록하는 것은 현재 동작과 "건드리면 깨지는 것"뿐이므로, 동작을 바꾸면 그 서술도 같은 변경에서 함께 고쳐야 한다.
