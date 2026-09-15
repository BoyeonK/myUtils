# Progress

최종 업데이트: 2026-09-15

---

## 완료

### 네트워크 / SendBuffer

- [x] (2026-09-15 #2) chunk 수명을 refcount로 전환하면서 `SendBufferManager`가 Current Chunk 참조를 하나 보유하도록 했다. outstanding `SendView`만 세면 마지막 I/O 완료 순간 사용 중인 chunk가 pool로 반환되어 두 worker가 같은 chunk에 동시 bump allocation하는 use-after-free가 발생한다. 이 참조 덕분에 "refcount == 0"이 "아무도 이 chunk를 모른다"와 정확히 같은 뜻이 된다.
- [x] (2026-09-15 #3) `SendBuffer` 계열을 레거시 코드와 양방향 분리. `ASSERT_CRASH`를 `MyUtils/Assert.h`로 추출해 PCH 암묵 의존(및 그에 딸려오던 `windows.h`, `using namespace std;`)을 끊었고, `ThreadManager::DestroyTLS()`의 `ReleaseCurrentChunk()` 호출도 제거했다. 후자는 `SendBufferManager`가 함수 지역 `thread_local`이라 소멸자가 알아서 반납하므로 불필요했다.
- [x] (2026-09-15 #10) 미결 질문 세 건(Q-001·Q-002·Q-003)을 결정하고 ADR로 기록했다. 측정 없이 열어두기만 하는 것보다 근거를 명시한 baseline을 정하고 문제가 관측될 때 다시 여는 편이 낫다고 판단했다. `DEFAULT_CHUNK_CAPACITY`를 64 KB로 올리고 `LARGE_ALLOCATION_THRESHOLD`를 16 KB로 분리했으며(ADR-0008), 그 결과 chunk 획득이 1,197→289회, 교체 시 폐기 바이트가 1.44 MB→0.33 MB로 줄었다.

### 코어 / 스레드

- [x] (2026-09-15 #9) 액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러를 저장소에서 제거했다. 전부 전면 재작성 대상이라, 그 위에 무언가를 쌓는 것보다 비우고 다시 세우는 편이 낫다고 판단했다. 연쇄로 `ThreadManager`의 액터 구동 메서드 2개도 정리했다.
- [x] (2026-09-15 #11) 남아 있던 죽은 코드를 전부 걷어냈다. 전역 4개(`GThreadManager`, `MyThreadID`, `LEndTickCount`, `LRanGen`)가 선언만 되고 쓰이는 곳이 없어 `GlobalVariables` 파일 쌍과 함께 지웠고, 초기화 대상이 사라지면서 빈 껍데기가 된 `InitTLS`/`DestroyTLS`도 제거했다. 그 결과 `pch.h`를 실제로 쓰는 `.cpp`가 하나도 남지 않아 PCH까지 지웠다 — 아무도 안 쓰면서 `<windows.h>`와 `using namespace std;`를 전 TU에 끌고 들어오고 있었다.

### 빌드 · 테스트 인프라

- [x] (2026-09-15 #4) MSVC `/utf-8` 옵션 추가. UTF-8 소스를 CP949로 읽으면서 한글 주석 끝 바이트가 lead byte로 오해돼 줄바꿈을 삼키고 다음 줄 선언이 주석에 먹히는 실제 빌드 실패가 있었다(증상은 "멀쩡히 있는 멤버를 못 찾는다"는 C2039). 공개 헤더에도 한글 주석이 있어 PUBLIC으로 전파한다.
- [x] (2026-09-15 #5) `tests/` 타겟과 ctest 연결. `SendBufferTest`가 tail reclaim 3경로, chunk 교체 중 이전 chunk 생존, pool 반환 타이밍, 크로스 스레드 해제, `SendView` 복사 fanout, 그리고 패턴 검증으로 영역 겹침을 잡는 멀티스레드 스트레스까지 164개 항목을 검사한다. 단독 빌드일 때만 켜지므로(`MYUTILS_BUILD_TESTS`) 소비자 프로젝트에는 영향이 없다.
- [x] (2026-09-15 #6) `.gitignore`에 `build/`, `out/`, `images/portfolio/` 추가.

### 문서

- [x] (2026-09-15 #7) 설계 근거를 코드 주석에서 `design/adr/`의 ADR 6개로 옮겼다. 주석이 지나치게 빡빡해 읽기 어려웠고, "왜 이 모양인가"는 코드 옆에 둘 이유가 없다. 대신 "이 줄을 바꾸면 깨지는 것"은 코드에 남기고 ADR 포인터를 10곳에 심었으며, `CLAUDE.md`의 송신 버퍼 섹션도 결정 근거 나열에서 지켜야 할 규칙 5개로 축소했다.
- [x] (2026-09-15 #8) `design/OPEN_QUESTIONS.md` 신설. 여러 세션과 다른 AI 에이전트가 같은 문서를 보고 이어서 논의하는 것이 목적이라, 각 항목에 "무엇이 이 질문을 닫는가"를 못박고 서명·날짜, ADR 재논쟁 금지, 측정 대기 항목에 의견 쌓지 않기를 참여 규칙으로 명시했다. `progress.md` TODO에서 결정 대기 5건을 빼고 링크만 남겨 중복을 없앴다.

---

## TODO

**오늘 바로 착수할 수 있는 것**만 여기 적는다. 먼저 골라야 할 게 있는 사안은
[`design/OPEN_QUESTIONS.md`](design/OPEN_QUESTIONS.md)에 있다.

### 높음

- [ ] `ThreadManager` 재작성. 현재는 `std::thread`를 벡터에 모아 join하는 것이 전부다. **worker를 객체로 표현할지(Q-006)를 먼저 정해야 방향이 잡힌다.** 재작성 시 전역 변수를 다시 들이지 말 것 — 임의 스레드에서 쓸 수 있는 성질이 거기서 나온다.

### 중간

- [ ] 대표 워크로드에서 `SnapshotStats()` 수집. 열린 질문이 이걸 기다리는 건 아니고, **ADR-0007·ADR-0008이 정한 재검토 조건을 판정하기 위한** 것이다. 두 문서의 조건 표가 어느 지표를 보면 되는지 알려준다.
- [ ] 계측 두 가지 추가. 둘 다 ADR의 재검토 조건인데 **현재 관측할 수단이 없다.** ① chunk 고정 시간 또는 세션당 pending `SendView` 수 (ADR-0007 조건 2), ② 살아있는 chunk의 **바이트** 합계 — 전용 chunk는 용량이 제각각이라 개수 × `CAPACITY`로는 부정확하다 (ADR-0008 Consequences).

### 낮음

- [ ] 액터/메시지 시스템 재구축. 2026-09-15에 기존 구현을 제거했으므로 백지에서 시작한다. 직전 구현은 git 히스토리(`2ce7019` 이전)에 있고, 거기서 가져갈 성질은 "액터 하나는 동시에 최대 한 스레드에서만 실행된다"는 계약과 weak_ptr 기반 태스크 정도다. 메시지 한 건당 힙 할당 0회를 목표로 잡을 것.

---

## 결정 대기

내용은 [`design/OPEN_QUESTIONS.md`](design/OPEN_QUESTIONS.md)에 있다. 여기에는 목록만 둔다.

| # | 질문 | 막힌 이유 |
|---|---|---|
| Q-006 | `WorkerContext` 개념을 도입할 것인가 | 설계 판단 (지향의 문제) |

닫힌 질문(전부 2026-09-15): **Q-001**(해결, baseline 튜닝 정책 — ADR-0008), **Q-002**(해결, `shared_ptr` 유지 — ADR-0003 후속 절), **Q-003**(해결, standalone 경로 미도입 — ADR-0007), **Q-004·Q-005·Q-007**(무효, 대상 코드 제거). 번호는 재사용하지 않는다.

---

## 알려진 이슈 · 메모

- `ASSERT_CRASH`에 `NDEBUG` 가드가 없어 릴리즈에서도 프로세스가 죽는다. 의도된 동작이므로 계약 위반에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- 2026-09-15에 액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러, 그리고 전역 변수 일체를 제거했다. 라이브러리는 현재 **송신 버퍼 + 얇은 스레드 런처**뿐이다. 직전 구현은 git 히스토리(`2ce7019` 이전)에 있다.
- **전역 변수와 `thread_local` 전역이 하나도 없다.** `MyThreadID`·`LEndTickCount`·`LRanGen`·`GThreadManager`가 전부 선언만 되고 쓰이지 않던 죽은 코드여서 함께 정리했다. 재작성 과정에서 다시 들이지 말 것.
- **PCH도 없다.** 남은 두 `.cpp` 모두 필요한 것을 직접 include한다. 다시 도입하면 `<windows.h>`와 `using namespace std;`가 전 TU에 끌려 들어온다.
- `SnapshotStats()`는 살아 있는 스레드의 카운터를 잠금 없이 읽는다. 통계 목적의 의도된 benign race이며, 정확한 값이 필요하면 대상 스레드를 join한 뒤 읽어야 한다.
- 소스와 헤더는 BOM 없는 UTF-8이다. MSVC에서 `/utf-8`을 빼면 조용히 깨진다.
- 문서는 네 곳으로 나뉜다 — 코드 주석 / `CLAUDE.md` / `design/adr/` / 이 파일. 어디에 무엇을 쓰는지는 `CLAUDE.md`의 "문서 구조" 절에 있다. 같은 내용을 두 곳에 쓰지 말 것.
