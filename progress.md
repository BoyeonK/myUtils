# Progress

최종 업데이트: 2026-09-15

---

## 완료

### 네트워크 / SendBuffer

- [x] (2026-09-15 #10) 미결 질문 세 건(Q-001·Q-002·Q-003)을 결정하고 ADR로 기록했다. 측정 없이 열어두기만 하는 것보다 근거를 명시한 baseline을 정하고 문제가 관측될 때 다시 여는 편이 낫다고 판단했다. `DEFAULT_CHUNK_CAPACITY`를 64 KB로 올리고 `LARGE_ALLOCATION_THRESHOLD`를 16 KB로 분리했으며(ADR-0008), 그 결과 chunk 획득이 1,197→289회, 교체 시 폐기 바이트가 1.44 MB→0.33 MB로 줄었다.

### Actor Runtime

- [x] (2026-09-15 #12) Actor Runtime 설계를 동결하고 문서로 고정했다. 요구사항·리뷰·동결 권고 세 문서에 흩어져 있던 결정 16개를 실행 규약(`design/actor-runtime.md`)과 ADR-0010~0012로 역할을 나눠 옮기고, 내용이 전부 흡수된 원본 세 문서는 삭제했다. 그 과정에서 `Stop()`의 이전 상태를 `load`+`store`로 잡으면 Finalize 주체 판정이 틀리는 경합과, 어느 문서에도 없던 "`Handle()`은 mailbox 락 밖에서 호출한다" invariant를 찾아 반영했다.
- [x] (2026-09-15 #13) Actor Runtime 1차 구현과 correctness test 13종. 설계에서 정한 1차 범위(단일 global runnable queue, work stealing 제외)까지이며, 스펙의 invariant ↔ test 표를 그대로 테스트 이름으로 옮겨 문서가 코드와 어긋나면 테스트가 깨지게 했다. Debug/Release 각 5회 반복에서 전부 통과했다.

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

- [ ] `ThreadManager` 정리. ADR-0009로 방향이 정해지면서 **재작성 범위가 거의 사라졌다** — 아무도 의존할 수 없는 편의 유틸리티이므로 지금의 launch/join 그대로면 충분하다. 남은 판단은 "이 정도면 라이브러리에 둘 가치가 있는가"뿐이고, 새 컴포넌트를 붙일 때 다시 보면 된다.

### 중간

- [ ] 대표 워크로드에서 `SnapshotStats()` 수집. 열린 질문이 이걸 기다리는 건 아니고, **ADR-0007·ADR-0008이 정한 재검토 조건을 판정하기 위한** 것이다. 두 문서의 조건 표가 어느 지표를 보면 되는지 알려준다.
- [ ] 계측 두 가지 추가. 둘 다 ADR의 재검토 조건인데 **현재 관측할 수단이 없다.** ① chunk 고정 시간 또는 세션당 pending `SendView` 수 (ADR-0007 조건 2), ② 살아있는 chunk의 **바이트** 합계 — 전용 chunk는 용량이 제각각이라 개수 × `CAPACITY`로는 부정확하다 (ADR-0008 Consequences).

### 낮음

- [ ] Actor Runtime에 work stealing 도입. semantics가 검증됐으므로 이제 scheduler만 교체하면 된다. **`EnqueueRunnable`을 우회하는 경로를 만들지 않는 것**이 회귀 방지의 핵심이다 — 우회하면 그 경로에서만 worker가 깨지 않는다.
- [ ] Actor Runtime 계측. 현재 관측 수단이 `LiveActorCount()` 하나뿐이라 budget 32가 적절한지, mailbox mutex가 contention 지점인지 판단할 수 없다. ADR-0011의 재검토 조건이 전부 이 데이터를 기다린다.

---

## 결정 대기

**현재 없다.** Q-001~Q-007이 전부 닫혔다. 새 질문이 열리면 [`design/OPEN_QUESTIONS.md`](design/OPEN_QUESTIONS.md)에 쓰고 여기에는 목록만 둔다.

닫힌 질문(전부 2026-09-15): **Q-001**(해결, baseline 튜닝 정책 — ADR-0008), **Q-002**(해결, `shared_ptr` 유지 — ADR-0003 후속 절), **Q-003**(해결, standalone 경로 미도입 — ADR-0007), **Q-006**(해결, `WorkerContext` 미도입 — ADR-0009), **Q-004·Q-005·Q-007**(무효, 대상 코드 제거). 번호는 재사용하지 않으며 새 질문은 Q-008부터다.

---

## 알려진 이슈 · 메모

- `ASSERT_CRASH`에 `NDEBUG` 가드가 없어 릴리즈에서도 프로세스가 죽는다. 의도된 동작이므로 계약 위반에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- 2026-09-15에 액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러, 그리고 전역 변수 일체를 제거했다. 라이브러리는 현재 **송신 버퍼 + 얇은 스레드 런처**뿐이다. 직전 구현은 git 히스토리(`2ce7019` 이전)에 있다.
- **전역 변수와 `thread_local` 전역이 하나도 없다.** `MyThreadID`·`LEndTickCount`·`LRanGen`·`GThreadManager`가 전부 선언만 되고 쓰이지 않던 죽은 코드여서 함께 정리했다. 재작성 과정에서 다시 들이지 말 것.
- **PCH도 없다.** 남은 두 `.cpp` 모두 필요한 것을 직접 include한다. 다시 도입하면 `<windows.h>`와 `using namespace std;`가 전 TU에 끌려 들어온다.
- `SnapshotStats()`는 살아 있는 스레드의 카운터를 잠금 없이 읽는다. 통계 목적의 의도된 benign race이며, 정확한 값이 필요하면 대상 스레드를 join한 뒤 읽어야 한다.
- 소스와 헤더는 BOM 없는 UTF-8이다. MSVC에서 `/utf-8`을 빼면 조용히 깨진다.
- 문서는 네 곳으로 나뉜다 — 코드 주석 / `CLAUDE.md` / `design/adr/` / 이 파일. 어디에 무엇을 쓰는지는 `CLAUDE.md`의 "문서 구조" 절에 있다. 같은 내용을 두 곳에 쓰지 말 것.
