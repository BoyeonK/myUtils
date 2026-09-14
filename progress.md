# Progress

최종 업데이트: 2026-09-15

---

## 완료

### 네트워크 / SendBuffer

- [x] (2026-09-15 #1) 낡은 `Network.h`/`Network.cpp`의 SendBuffer를 폐기하고 `SendBuffer.h`/`SendBuffer.cpp`로 재설계. 기존 구현은 `Close()`를 한 번만 빠뜨려도 chunk의 `_isOpen`이 영구히 true로 남아 다음 `Open`에서 프로세스가 죽는 구조였고, 그걸 막으려던 가드조차 chunk 교체 시 우회됐다. `Reserve` → `Commit` → `SendView` 타입 전이로 잘못된 사용을 컴파일 단계에서 막고, 미커밋 소멸은 소멸자가 자동 회수하도록 바꿨다.
- [x] (2026-09-15 #2) chunk 수명을 refcount로 전환하면서 `SendBufferManager`가 Current Chunk 참조를 하나 보유하도록 했다. outstanding `SendView`만 세면 마지막 I/O 완료 순간 사용 중인 chunk가 pool로 반환되어 두 worker가 같은 chunk에 동시 bump allocation하는 use-after-free가 발생한다. 이 참조 덕분에 "refcount == 0"이 "아무도 이 chunk를 모른다"와 정확히 같은 뜻이 된다.
- [x] (2026-09-15 #3) `SendBuffer` 계열을 레거시 코드와 양방향 분리. `ASSERT_CRASH`를 `MyUtils/Assert.h`로 추출해 PCH 암묵 의존(및 그에 딸려오던 `windows.h`, `using namespace std;`)을 끊었고, `ThreadManager::DestroyTLS()`의 `ReleaseCurrentChunk()` 호출도 제거했다. 후자는 `SendBufferManager`가 함수 지역 `thread_local`이라 소멸자가 알아서 반납하므로 불필요했다.

### 빌드 · 테스트 인프라

- [x] (2026-09-15 #4) MSVC `/utf-8` 옵션 추가. UTF-8 소스를 CP949로 읽으면서 한글 주석 끝 바이트가 lead byte로 오해돼 줄바꿈을 삼키고 다음 줄 선언이 주석에 먹히는 실제 빌드 실패가 있었다(증상은 "멀쩡히 있는 멤버를 못 찾는다"는 C2039). 공개 헤더에도 한글 주석이 있어 PUBLIC으로 전파한다.
- [x] (2026-09-15 #5) `tests/` 타겟과 ctest 연결. `SendBufferTest`가 tail reclaim 3경로, chunk 교체 중 이전 chunk 생존, pool 반환 타이밍, 크로스 스레드 해제, `SendView` 복사 fanout, 그리고 패턴 검증으로 영역 겹침을 잡는 멀티스레드 스트레스까지 164개 항목을 검사한다. 단독 빌드일 때만 켜지므로(`MYUTILS_BUILD_TESTS`) 소비자 프로젝트에는 영향이 없다.
- [x] (2026-09-15 #6) `.gitignore`에 `build/`, `out/`, `images/portfolio/` 추가.

### 문서

- [x] (2026-09-15 #0) `CLAUDE.md` 신규 작성. PCH 암묵 의존, 공개 헤더 자립 규칙, 액터 단일 실행 계약, 스케줄러의 단일 스레드 요구 등 여러 파일을 같이 읽어야 파악되는 것들을 정리했다.

---

## TODO

### 높음

- [ ] 레거시 코드 전면 정비 (`pch.h` 포함). 정비 시 각 `.cpp`가 PCH 대신 `MyUtils/Assert.h`를 직접 include하도록 옮길 것. `pch.h`에 `NOMINMAX`가 없어 `min`/`max` 매크로가 살아 있는 것도 같이 판단.
- [ ] `ObjectPool` 스레드 종료 시 로컬 프리리스트 누수. `thread_local std::vector<T*>`가 소멸할 때 최대 2000개 블록을 전역 풀로 돌려주지 않고 버린다. 스레드를 반복 생성/조인하면 누적된다.
- [ ] `ObjectPool` TLS 소멸 순서에 따른 UAF 가능성. `shared_ptr` deleter가 `GetLocalPool()`을 호출하므로, 풀 객체를 든 다른 `thread_local`이 로컬 풀보다 늦게 소멸하면 파괴된 vector를 건드린다. 타입마다 등록 순서가 달라 재현이 불규칙하다.

### 중간

- [ ] `Actor::PostTask` hot path의 할당 제거. `ObjectPool<Task>::Acquire`가 `shared_ptr` 커스텀 deleter를 쓰는 탓에 메시지마다 control block malloc이 발생하고, `Task`의 `std::function`도 캡처 크기에 따라 SBO를 넘기면 추가 할당한다. 풀을 도입한 이득이 상당 부분 상쇄된다.
- [ ] `SendBuffer` 실측 후 튜닝. `DEFAULT_CHUNK_CAPACITY`(16 KB), `LARGE_ALLOCATION_THRESHOLD`, `DEFAULT_POOL_IDLE_LIMIT`은 전부 correctness와 무관한 값이다. 판단 근거는 `SnapshotStats()`의 이용률, 살아있는 chunk 수, large path 비율, pool 히트율.
- [ ] `ObjectPool` 자잘한 결함들: `::operator new(sizeof(T))`가 over-aligned 타입의 정렬을 보장하지 않음, `enqueue_bulk` 반환값 무시로 OOM 시 블록 유실, `AcquireRaw`/`ReturnRaw` 비대칭(후자가 소멸자를 부르지 않음), `MAX_LOCAL_SIZE` 임계 비교가 `>`라서 첫 플러시마다 벡터 재할당.

### 낮음

- [ ] `TLTask` / `TLTaskQueue` 소비 루프 구현. 선언만 되어 있고 아직 어떤 루프도 처리하지 않는다.
- [ ] 브로드캐스트 pinning 대응 검토. fanout이 큰 패킷을 exact-sized standalone 버퍼로 빼면 느린 클라이언트가 chunk 전체를 붙잡는 것을 막을 수 있다. 계측으로 실제 문제인지 확인한 뒤 판단.
- [ ] `ChunkRef`를 intrusive refcount로 전환 검토. 타입 별칭으로 격리해뒀으므로 교체 비용은 낮지만, control block 비용이 chunk 수명마다 발생하므로 profiling 전에는 불필요.

---

## 알려진 이슈 · 메모

- `ASSERT_CRASH`에 `NDEBUG` 가드가 없어 릴리즈에서도 프로세스가 죽는다. 의도된 동작이므로 계약 위반에만 쓰고, 복구 가능한 실패는 반환값으로 알릴 것.
- `Actor::_messageCount`는 `Push`에서 증가만 하고 어디서도 감소하지 않으며 읽는 곳도 없다. `prevCount` 지역 변수도 사용되지 않는다. 미완성으로 보이므로 액터 정비 시 의도를 확인할 것.
- `ActorMessageScheduler::AddAndDistribute`는 `_orderedMessages`를 락 없이 다루므로 **반드시 한 스레드에서만** 호출해야 한다. 이 레포에는 호출하는 루프가 없으므로 소비자 쪽 규율에 달려 있다.
- `SnapshotStats()`는 살아 있는 스레드의 카운터를 잠금 없이 읽는다. 통계 목적의 의도된 benign race이며, 정확한 값이 필요하면 대상 스레드를 join한 뒤 읽어야 한다.
- 소스와 헤더는 BOM 없는 UTF-8이다. MSVC에서 `/utf-8`을 빼면 조용히 깨진다.
