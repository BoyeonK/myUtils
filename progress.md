# Progress

최종 업데이트: 2026-09-15

---

## 완료

### 네트워크 / SendBuffer

- [x] (2026-09-15 #1) 낡은 `Network.h`/`Network.cpp`의 SendBuffer를 폐기하고 `SendBuffer.h`/`SendBuffer.cpp`로 재설계. 기존 구현은 `Close()`를 한 번만 빠뜨려도 chunk의 `_isOpen`이 영구히 true로 남아 다음 `Open`에서 프로세스가 죽는 구조였고, 그걸 막으려던 가드조차 chunk 교체 시 우회됐다. `Reserve` → `Commit` → `SendView` 타입 전이로 잘못된 사용을 컴파일 단계에서 막고, 미커밋 소멸은 소멸자가 자동 회수하도록 바꿨다.
- [x] (2026-09-15 #2) chunk 수명을 refcount로 전환하면서 `SendBufferManager`가 Current Chunk 참조를 하나 보유하도록 했다. outstanding `SendView`만 세면 마지막 I/O 완료 순간 사용 중인 chunk가 pool로 반환되어 두 worker가 같은 chunk에 동시 bump allocation하는 use-after-free가 발생한다. 이 참조 덕분에 "refcount == 0"이 "아무도 이 chunk를 모른다"와 정확히 같은 뜻이 된다.
- [x] (2026-09-15 #3) `SendBuffer` 계열을 레거시 코드와 양방향 분리. `ASSERT_CRASH`를 `MyUtils/Assert.h`로 추출해 PCH 암묵 의존(및 그에 딸려오던 `windows.h`, `using namespace std;`)을 끊었고, `ThreadManager::DestroyTLS()`의 `ReleaseCurrentChunk()` 호출도 제거했다. 후자는 `SendBufferManager`가 함수 지역 `thread_local`이라 소멸자가 알아서 반납하므로 불필요했다.

### 코어 / 스레드

- [x] (2026-09-15 #9) 액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러를 저장소에서 제거했다. 전부 전면 재작성 대상이라, 그 위에 무언가를 쌓는 것보다 비우고 다시 세우는 편이 낫다고 판단했다. 연쇄로 `GlobalVariables`의 전역 4개와 `ThreadManager`의 액터 구동 메서드 2개도 정리해 라이브러리는 송신 버퍼 + 스레드 런처만 남았다.

### 빌드 · 테스트 인프라

- [x] (2026-09-15 #4) MSVC `/utf-8` 옵션 추가. UTF-8 소스를 CP949로 읽으면서 한글 주석 끝 바이트가 lead byte로 오해돼 줄바꿈을 삼키고 다음 줄 선언이 주석에 먹히는 실제 빌드 실패가 있었다(증상은 "멀쩡히 있는 멤버를 못 찾는다"는 C2039). 공개 헤더에도 한글 주석이 있어 PUBLIC으로 전파한다.
- [x] (2026-09-15 #5) `tests/` 타겟과 ctest 연결. `SendBufferTest`가 tail reclaim 3경로, chunk 교체 중 이전 chunk 생존, pool 반환 타이밍, 크로스 스레드 해제, `SendView` 복사 fanout, 그리고 패턴 검증으로 영역 겹침을 잡는 멀티스레드 스트레스까지 164개 항목을 검사한다. 단독 빌드일 때만 켜지므로(`MYUTILS_BUILD_TESTS`) 소비자 프로젝트에는 영향이 없다.
- [x] (2026-09-15 #6) `.gitignore`에 `build/`, `out/`, `images/portfolio/` 추가.

### 문서

- [x] (2026-09-15 #0) `CLAUDE.md` 신규 작성. PCH 암묵 의존, 공개 헤더 자립 규칙, 액터 단일 실행 계약, 스케줄러의 단일 스레드 요구 등 여러 파일을 같이 읽어야 파악되는 것들을 정리했다.
- [x] (2026-09-15 #7) 설계 근거를 코드 주석에서 `design/adr/`의 ADR 6개로 옮겼다. 주석이 지나치게 빡빡해 읽기 어려웠고, "왜 이 모양인가"는 코드 옆에 둘 이유가 없다. 대신 "이 줄을 바꾸면 깨지는 것"은 코드에 남기고 ADR 포인터를 10곳에 심었으며, `CLAUDE.md`의 송신 버퍼 섹션도 결정 근거 나열에서 지켜야 할 규칙 5개로 축소했다.
- [x] (2026-09-15 #8) `design/OPEN_QUESTIONS.md` 신설. 여러 세션과 다른 AI 에이전트가 같은 문서를 보고 이어서 논의하는 것이 목적이라, 각 항목에 "무엇이 이 질문을 닫는가"를 못박고 서명·날짜, ADR 재논쟁 금지, 측정 대기 항목에 의견 쌓지 않기를 참여 규칙으로 명시했다. `progress.md` TODO에서 결정 대기 5건을 빼고 링크만 남겨 중복을 없앴다.

---

## TODO

**오늘 바로 착수할 수 있는 것**만 여기 적는다. 먼저 골라야 할 게 있는 사안은
[`design/OPEN_QUESTIONS.md`](design/OPEN_QUESTIONS.md)에 있다.

### 높음

- [ ] `pch.h` 정리. 레거시를 걷어낸 뒤 PCH에 의존하는 파일은 `GlobalVariables.cpp`와 `Thread.cpp` 둘뿐이고 둘 다 작다. 두 파일이 `MyUtils/Assert.h`를 직접 include하도록 옮기면 PCH 자체를 없앨 수 있는지 판단할 것. `NOMINMAX`가 없어 `min`/`max` 매크로가 살아 있는 것도 같이 본다.

### 중간

- [ ] 대표 워크로드에서 `SnapshotStats()` 수집. Q-001·Q-002·Q-003이 전부 이 데이터를 기다리고 있어서, 이걸 하면 질문 세 개가 한 번에 닫힌다.

### 낮음

- [ ] 액터/메시지 시스템 재구축. 2026-09-15에 기존 구현을 제거했으므로 백지에서 시작한다. 직전 구현은 git 히스토리(`2ce7019` 이전)에 있고, 거기서 가져갈 성질은 "액터 하나는 동시에 최대 한 스레드에서만 실행된다"는 계약과 weak_ptr 기반 태스크 정도다. 메시지 한 건당 힙 할당 0회를 목표로 잡을 것.

---

## 결정 대기

내용은 [`design/OPEN_QUESTIONS.md`](design/OPEN_QUESTIONS.md)에 있다. 여기에는 목록만 둔다.

| # | 질문 | 막힌 이유 |
|---|---|---|
| Q-001 | SendBuffer 튜닝 값 세 개 | 측정 |
| Q-002 | ChunkRef를 intrusive refcount로 바꿀 것인가 | 측정 |
| Q-003 | 브로드캐스트·coalescing pinning에 대응할 것인가 | 측정 |
| Q-006 | `WorkerContext` 개념을 도입할 것인가 | 설계 판단 (지향의 문제) |

Q-004·Q-005·Q-007은 2026-09-15 무효 처리했다. 대상 코드(`Actor`, `ObjectPool`)를 저장소에서 제거했기 때문이다. 번호는 재사용하지 않는다.

---

## 알려진 이슈 · 메모

- `ASSERT_CRASH`에 `NDEBUG` 가드가 없어 릴리즈에서도 프로세스가 죽는다. 의도된 동작이므로 계약 위반에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- 2026-09-15에 액터/메시지 시스템, 오브젝트 풀, MPSC 큐, 타이머 스케줄러를 전부 제거했다. 라이브러리는 현재 **송신 버퍼 + 스레드 런처**뿐이다. 직전 구현은 git 히스토리(`2ce7019` 이전)에 있다.
- `SnapshotStats()`는 살아 있는 스레드의 카운터를 잠금 없이 읽는다. 통계 목적의 의도된 benign race이며, 정확한 값이 필요하면 대상 스레드를 join한 뒤 읽어야 한다.
- 소스와 헤더는 BOM 없는 UTF-8이다. MSVC에서 `/utf-8`을 빼면 조용히 깨진다.
- 문서는 네 곳으로 나뉜다 — 코드 주석 / `CLAUDE.md` / `design/adr/` / 이 파일. 어디에 무엇을 쓰는지는 `CLAUDE.md`의 "문서 구조" 절에 있다. 같은 내용을 두 곳에 쓰지 말 것.
