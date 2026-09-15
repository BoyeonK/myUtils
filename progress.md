# Progress

최종 업데이트: 2026-09-15

---

## 완료

### 네트워크 / SendBuffer

- [x] (2026-09-15 #10) 미결 질문 세 건(Q-001·Q-002·Q-003)을 결정하고 ADR로 기록했다. 측정 없이 열어두기만 하는 것보다 근거를 명시한 baseline을 정하고 문제가 관측될 때 다시 여는 편이 낫다고 판단했다. `DEFAULT_CHUNK_CAPACITY`를 64 KB로 올리고 `LARGE_ALLOCATION_THRESHOLD`를 16 KB로 분리했으며(ADR-0008), 그 결과 chunk 획득이 1,197→289회, 교체 시 폐기 바이트가 1.44 MB→0.33 MB로 줄었다.

### Actor Runtime

- [x] (2026-09-15 #12) Actor Runtime 설계를 동결하고 문서로 고정했다. 요구사항·리뷰·동결 권고 세 문서에 흩어져 있던 결정 16개를 실행 규약(`design/actor-runtime.md`)과 ADR-0010~0012로 역할을 나눠 옮기고, 내용이 전부 흡수된 원본 세 문서는 삭제했다. 그 과정에서 `Stop()`의 이전 상태를 `load`+`store`로 잡으면 Finalize 주체 판정이 틀리는 경합과, 어느 문서에도 없던 "`Handle()`은 mailbox 락 밖에서 호출한다" invariant를 찾아 반영했다.
- [x] (2026-09-15 #13) Actor Runtime 1차 구현과 correctness test 13종. 설계에서 정한 1차 범위(단일 global runnable queue, work stealing 제외)까지이며, 스펙의 invariant ↔ test 표를 그대로 테스트 이름으로 옮겨 문서가 코드와 어긋나면 테스트가 깨지게 했다. Debug/Release 각 5회 반복에서 전부 통과했다.
- [x] (2026-09-15 #16) 스케줄러의 목표 구조(worker-local work-stealing deque + global injection queue)를 문서에 복원했다. 현재의 단일 global queue가 최종 architecture decision인 것처럼 문서화되어 있었는데, 실제로는 correctness를 먼저 검증하려고 단순화한 baseline scheduler다. 요구사항 원문을 삭제할 때 끊어진 링크만 확인하고 그 문서에만 있던 정보를 확인하지 않은 것이 원인이다.
- [x] (2026-09-15 #17) drain 루프에 consumption boundary를 추가했다. `Stop()`은 state만 바꿀 뿐 worker는 여전히 루프 안이라, pop 전에 상태를 재확인하지 않으면 Stop 이후에도 pending 메시지를 budget 한도까지 처리해 "pending을 버린다"는 계약이 타이밍에 따라 0~31건 사이로 흔들렸다. 검사를 mailbox 락 안에 둬서 "Stop이 그 락을 통과한 뒤로는 어떤 메시지도 새로 pop되지 않는다"가 성립하게 했고, `SelfStopFromHandler`가 pending 미실행까지 검증하도록 강화했다(검사를 빼면 `handled == 4`로 깨지는 것을 확인).
- [x] (2026-09-15 #18) 2차 스케줄러를 구현했다. 단일 global queue를 injection queue로 축소하고 worker-local deque(worker당 mutex + deque)와 work stealing을 붙였으며, 두 큐의 notify 성격이 다르다는 점(injection은 correctness, local은 heuristic)에 따라 알림 정책을 분리했다(Q-008·Q-009 → ADR-0014). 구현 중 문서에 없던 함정 둘을 찾아 테스트로 고정했다 — 다중 `ActorSystem`에서 TLS가 worker index만 담으면 runnable이 남의 deque로 새는 것(`CrossRuntimeIsolation`), local 우선만 두면 injection이 굶는 것(`InjectionNotStarvedByLocalWork`).
- [x] (2026-09-15 #19) `Spawn()`과 `Shutdown()`의 Registry 등록 경합을 수정했다. `Spawn`이 accepting 확인을 통과한 뒤 ACB 할당에서 멈추고, 그 사이 `Shutdown`이 빈 Registry의 snapshot/clear를 끝내면 종료 후 Actor가 등록되어 `LiveActorCount()==1`이고 `Send()==true`가 되는 실행을 결정적으로 재현했다. accepting gate 변경과 Registry 등록을 같은 Registry 락으로 선형화해, 먼저 등록된 Actor는 shutdown snapshot에 반드시 포함되고 늦은 등록은 무효 `ActorRef`로 거부되게 했다. `SpawnConcurrentWithShutdownRejected`가 해당 경합 창을 고정한다.

### 코어 / 스레드

- [x] (2026-09-15 #9) 액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러를 저장소에서 제거했다. 전부 전면 재작성 대상이라, 그 위에 무언가를 쌓는 것보다 비우고 다시 세우는 편이 낫다고 판단했다. 연쇄로 `ThreadManager`의 액터 구동 메서드 2개도 정리했다.
- [x] (2026-09-15 #11) 남아 있던 죽은 코드를 전부 걷어냈다. 전역 4개(`GThreadManager`, `MyThreadID`, `LEndTickCount`, `LRanGen`)가 선언만 되고 쓰이는 곳이 없어 `GlobalVariables` 파일 쌍과 함께 지웠고, 초기화 대상이 사라지면서 빈 껍데기가 된 `InitTLS`/`DestroyTLS`도 제거했다. 그 결과 `pch.h`를 실제로 쓰는 `.cpp`가 하나도 남지 않아 PCH까지 지웠다 — 아무도 안 쓰면서 `<windows.h>`와 `using namespace std;`를 전 TU에 끌고 들어오고 있었다.
- [x] (2026-09-15 #15) 쓰이지 않는데다 깨져 있던 `ThreadManager`를 제거했다. 라이브러리·테스트 어디에서도 쓰지 않았고 하는 일이 `vector<thread>` + join 루프여서 편의성 이득이 5줄이었다. 게다가 `Launch`는 뮤텍스를 잡는데 `Join`은 잡지 않아 동시 호출 시 iterator 무효화와 data race가 났다 — 뮤텍스가 있어서 동기화된 것처럼 보이는 게 더 나빴다.

### 계측

- [x] (2026-09-15 #14) 계측을 비용 3단으로 나눠 구현했다. ADR 다섯 개가 "이 조건이 관측되면 재검토한다"고 달아뒀는데 정작 그 조건을 감지할 수단이 없어서, 트리거는 있고 알람은 없는 상태였다. chunk·배치 단위 지표는 메시지당 환산하면 0.2ns 미만이라 항상 켜두고, 메시지마다 시계를 읽어야 하는 mailbox 대기 시간만 런타임 플래그로 뺐다(ADR-0013).

---

## TODO

**오늘 바로 착수할 수 있는 것**만 여기 적는다. 먼저 골라야 할 게 있는 사안은
[`design/OPEN_QUESTIONS.md`](design/OPEN_QUESTIONS.md)에 있다.

### 중간

- [ ] **대표 워크로드에서 실측.** 계측은 갖춰졌으므로(ADR-0013) 이제 실제 컨텐츠를 붙이고 `SnapshotStats()`를 뽑으면 된다. ADR-0007·0008·0011·0014의 재검토 조건이 전부 이 데이터를 기다린다. 메시지 단위 지표가 필요하면 `SetProfilingEnabled(true)`를 켠다.

  스케줄러 쪽에서 특히 볼 것 — `stealSuccess/stealAttempt`(현재 테스트에서 0.4%. 잠들기 전 헛스캔이 비용으로 잡히면 victim 선택을 손볼 자리다), injection과 local의 큐 대기 시간 차이(`INJECTION_POLL_INTERVAL`=61 재검토), `notifySkipped/localPush`(조건부 notify가 실제로 아끼는 게 있는가).

---

## 결정 대기

- [ ] **Q-010 — worker-local LIFO의 Actor 간 공정성 semantics.** 단일 worker에서
  계속 재등록되는 Actor가 앞쪽의 오래된 Actor를 무기한 굶길 수 있음이 확인됐다.
  결정과 근거는 [`design/OPEN_QUESTIONS.md`](design/OPEN_QUESTIONS.md#q-010-worker-local-lifo가-actor를-무기한-굶겨도-되는가)에 둔다.

닫힌 질문(전부 2026-09-15): **Q-001**(해결, baseline 튜닝 정책 — ADR-0008), **Q-002**(해결, `shared_ptr` 유지 — ADR-0003 후속 절), **Q-003**(해결, standalone 경로 미도입 — ADR-0007), **Q-006**(해결, `WorkerContext` 미도입 — ADR-0009), **Q-008·Q-009**(해결, 2차 스케줄러 정책 — ADR-0014), **Q-004·Q-005·Q-007**(무효, 대상 코드 제거). 번호는 재사용하지 않으며 Q-010은 현재 결정 대기 중이다.

---

## 알려진 이슈 · 메모

- `ASSERT_CRASH`에 `NDEBUG` 가드가 없어 릴리즈에서도 프로세스가 죽는다. 의도된 동작이므로 계약 위반에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- 2026-09-15에 레거시(액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러, 전역 변수 일체)를 제거하고 Actor Runtime을 백지에서 다시 만들었다. 현재 구성은 **송신 버퍼 + Actor Runtime + 계측** 셋이다. 제거 직전 구현은 git 히스토리(`2ce7019` 이전)에 있다.
- **스레드를 만들거나 관리하는 코드가 라이브러리에 없다.** `ThreadManager`는 사용처가 0이었고 `Join()`에 data race까지 있어 2026-09-15에 삭제했다. Actor Runtime은 자기 worker를 직접 만든다.
- **전역 변수와 `thread_local` 전역이 하나도 없다.** `MyThreadID`·`LEndTickCount`·`LRanGen`·`GThreadManager`가 전부 선언만 되고 쓰이지 않던 죽은 코드여서 함께 정리했다. 재작성 과정에서 다시 들이지 말 것.
- **PCH도 없다.** 모든 `.cpp`가 필요한 것을 직접 include한다. 다시 도입하면 `<windows.h>`와 `using namespace std;`가 전 TU에 끌려 들어온다.
- `SnapshotStats()`는 살아 있는 스레드의 카운터를 잠금 없이 읽는다. 통계 목적의 의도된 benign race이며, 정확한 값이 필요하면 대상 스레드를 join한 뒤 읽어야 한다. 계측 단 구분은 ADR-0013.
- 소스와 헤더는 BOM 없는 UTF-8이다. MSVC에서 `/utf-8`을 빼면 조용히 깨진다.
- **문서를 지울 때는 끊어진 링크뿐 아니라 "그 문서가 유일한 출처인 정보"가 있는지 먼저 확인할 것.** 요구사항 원문을 지우면서 스케줄러 목표 구조를 통째로 잃은 전례가 있다(2026-09-15, `8d13fd1`에서 복원).
- **미커밋 변경이 있는 파일에 `git checkout <파일>`을 쓰지 말 것.** HEAD로 되돌아가 그 파일의 미커밋 작업이 전부 사라진다. 2026-09-15에 2차 스케줄러 구현을 이렇게 날렸다(재적용으로 복구). 코드를 임시로 바꿔 실험할 때는 먼저 사본을 떠두고 그것으로 되돌린다.
- **PowerShell `Set-Content`/`Out-File`로 소스 파일을 쓰지 말 것.** 한글 주석이 깨진다(2026-09-15에 `ActorRuntime.cpp`가 이걸로 깨졌다). 파일을 통째로 바꿔야 하면 Python 바이너리 모드나 `Copy-Item`처럼 바이트를 보존하는 수단을 쓴다.
- **ADR의 논증이 일부 경로에만 적용되어 있지 않은지 확인할 것.** ADR-0012는 "Stop은 state만 바꿀 뿐 worker는 여전히 drain 루프 안이다"라는 논증을 예외 경로에만 적고 일반 `Stop` 경로에는 적용하지 않았고, 구현도 그대로 빠뜨렸다. 같은 문서 안에 답이 있었던 전례다(2026-09-15).
- 문서는 다섯 곳으로 나뉜다 — 코드 주석 / `design/<컴포넌트>.md` / `CLAUDE.md` / `design/adr/` / 이 파일. 어디에 무엇을 쓰는지는 `CLAUDE.md`의 "문서 구조" 절에 있다. 같은 내용을 두 곳에 쓰지 말 것.
