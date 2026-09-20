# 송신 버퍼 실행 규약

구현자가 참고하는 **실행 규약**이다. 파일 하나만 봐서는 알 수 없는 것 — `SendBuffer.h`와
`SendBuffer.cpp`에 나뉘어 있는 타입들 사이의 수명·소유권 관계, 동기화 근거 — 만 여기에 둔다.

한 파일 안에서 끝나는 규칙과 타입별 계약은 그 코드 옆 주석에 있다. 특히 `SendBuffer.h`
상단의 `[용도]` 블록이 이 컴포넌트의 최적화 목표를 정의하므로 **정책을 건드리기 전에
그것을 먼저 읽는다.**

## 구조

```
Reserve(capacity) → SendBuffer (이동 전용, mutable)
                  → Commit(actualSize) → SendView (복사 가능, immutable)
```

thread-local bump allocator + refcounted arena다. 타입 넷이 다음과 같이 엮인다.

```
SendBufferManager (thread 하나당 하나)
   └── ChunkRef _current ──┐
                           ▼
                    SendBufferChunk ◄── ChunkRef ── SendView (복사본 전부)
                           ▲                    ◄── ChunkRef ── SendBuffer (예약 중)
                           │
                     refcount 0 → deleter
                           │
                           ▼
                    ChunkPool (프로세스 전역, MPMC 유휴 큐)
```

| 참조 | 이유 |
|---|---|
| Manager → Current Chunk (**shared**) | 아래 O1. 이 참조가 "사용 중"의 유일한 표현이다 |
| SendBuffer → Chunk (shared) | 예약이 살아 있는 동안 chunk를 붙잡는다 |
| SendView → Chunk (shared) | I/O가 끝날 때까지 chunk를 붙잡는다. 복사는 refcount 증가뿐 |
| chunk deleter → ChunkPool (**shared**) | pool이 자기가 내준 chunk 전부보다 오래 살아야 반환이 안전하다 |
| ChunkPool → 유휴 chunk (**raw**) | 순환 참조 방지 |

`ChunkPool`의 유휴 큐는 moodycamel `ConcurrentQueue`다.

---

## O1. Manager의 Current Chunk 참조를 없애지 않는다

> **`SendBufferManager`는 현재 bump allocation 중인 chunk에 대한 `ChunkRef`를 하나
> 들고 있다. 이 참조가 있어야 "refcount == 0"이 "아무도 이 chunk를 모른다"와 같은
> 뜻이 된다.**

없으면 마지막 `SendView`가 파괴되는 순간 — 즉 마지막 I/O가 끝나는 순간 — 아직 owner가
쓰고 있는 chunk가 pool로 반환된다. 다른 worker가 그것을 꺼내 가면 두 thread가 같은
chunk에 동시에 bump allocation한다.

이 참조는 tail reclaim의 owner 증명에도 쓰인다(아래 O3).

---

## O2. chunk 소유권 이전은 반드시 ChunkPool을 경유한다

`SendBufferChunk::_offset`은 원자 변수가 아니고 락으로도 보호되지 않는다. 그게 안전한
근거는 **한 시점에 owner thread가 하나뿐이고, 소유권이 넘어가는 유일한 경로가 pool
큐라서 enqueue/dequeue가 happens-before를 만들어주기** 때문이다.

```
이전 owner: _offset 갱신 ... → pool.enqueue(chunk)
                                        │ happens-before
새 owner:                        pool.dequeue(chunk) → Reset() → _offset 갱신 ...
```

`Reset()`은 **반환 시점이 아니라 새 owner가 쓰기 직전에** 호출한다. 위 순서에서 그 위치가
happens-before의 뒤쪽이기 때문이다.

> **worker 사이에서 chunk를 직접 넘기는 최적화를 넣는 순간 이 근거가 사라진다.**
> pool을 우회하면 `_offset`의 비동기화 상태가 그대로 data race가 된다.

---

## O3. tail reclaim은 LIFO 조건에서만 동작한다

`Commit(used)`과 `SendBuffer` 소멸자는 예약 꼬리를 회수하려 시도한다. 조건은 둘이고,
**서로 다른 것을 증명하므로 둘 다 필요하다.**

| 조건 | 증명하는 것 |
|---|---|
| Manager의 Current Chunk == 내 chunk | 내가 owner다. `_offset`을 동기화 없이 읽고 써도 된다 |
| chunk 커서 == 내 예약의 끝 | 내 뒤에 다른 할당이 없다. 남의 영역을 침범하지 않는다 |

첫 조건이 owner임을 증명하는 이유는 **chunk가 한 시점에 최대 하나의 manager에게만
Current일 수 있기** 때문이다(O1의 참조가 pool 반환을 막는다). 그래서 thread id 비교도
atomic도 필요 없다.

**뒤에 다른 할당이 끼면 회수되지 않는 것이 정상이다.** 버그가 아니라 조건 미충족이며,
회수 실패분은 `SnapshotStats()`의 `reservedBytes` 대비 `committedBytes` 차이로 보인다.

---

## O4. 호출자 계약

- **`Reserve` 반환값을 반드시 확인한다.** 크기 0 또는 `MAX_SEND_BUFFER_SIZE` 초과면 무효
  버퍼가 돌아온다. 확인 없이 쓰면 `WritableData()` / `Commit()`이 `MYUTILS_ASSERT`로 죽는다
  (계약 위반이므로 릴리즈에서도 죽는다).
- **`Reserve`는 정렬 올림을 하지 않는다.** 커서는 요청한 만큼만 정확히 전진하므로 이
  저장소는 byte serialization 전용이다. placement new로 typed object를 만들지 말 것 —
  필요해지면 `Reserve(size, alignment)` 오버로드를 추가한다.
- **`SendView`는 비동기 I/O completion context에 값으로 저장한다.** 호출자 쪽 수명에
  기대면 chunk가 조기 해제된다.
- **`SendBuffer`는 이동 전용이고 `Commit`이 자기 자신을 소비한다.** 확정 후 쓰기가 타입
  수준에서 막힌다. `Commit` 없이 소멸해도 예약은 가능한 경우 자동으로 되돌아간다.

---

## O5. 정책 상수

전부 튜닝 값이며 correctness와 무관하다. 현재 값(chunk 64 KB, large 임계 16 KB, pool
유휴 64)은 **측정에서 나온 최적값이 아니라 `SendBuffer.h`의 `[용도]`에 맞춰 정한
baseline이다.** 임의로 바꾸지 말고 `SnapshotStats()`로 문제를 먼저 확인한다.

두 가지는 방향이 정해져 있다.

- **pinning을 줄이려고 chunk 크기를 낮추지 않는다.** 느린 peer가 `SendView`를 오래
  붙잡아 생기는 memory pinning 최소화는 이 allocator의 목표가 아니다. 필요하면 상위
  session 정책에서 대응한다.
- **`LARGE_ALLOCATION_THRESHOLD`를 `DEFAULT_CHUNK_CAPACITY`와 같게 맞추지 않는다.**
  전자는 "current chunk를 교체시킬 만큼 큰 요청인가"를 판정하는 정책값이고 후자는 저장
  공간 크기라 의미가 다르다. 같게 두면 20~30 KB 요청 하나가 64 KB chunk를 갈아치우며
  남은 공간을 통째로 버린다.

---

## O6. 계측

공통 규약(항상 켜짐, 자가 등록 `thread_local` 패턴, 스냅샷의 benign race)은
[`instrumentation.md`](instrumentation.md)에 있다. 여기에는 이 컴포넌트 고유의 것만 둔다.

**메모리 압력을 볼 때는 `LiveChunkCount()`가 아니라 `LiveChunkBytes()`를 쓴다.** 전용
chunk는 용량이 제각각이라 `개수 × DEFAULT_CHUNK_CAPACITY`로 추정하면 부정확하다.

---

## Invariant ↔ Test

스펙이 코드와 어긋나는 것을 막는 장치다. 테스트는 `tests/SendBufferTest.cpp`에 있다.

| Invariant | Test |
|---|---|
| `Reserve` → `Commit` → `SendView`의 내용이 보존된다 | `TestBasic` |
| 크기 0 / 상한 초과 요청은 무효 버퍼를 반환한다 | `TestInvalidInputs` |
| LIFO 조건에서 tail이 회수되고, 아니면 회수되지 않는다 (O3) | `TestTailReclaim` |
| 이동 후 원본이 예약을 이중 회수하지 않는다 | `TestMoveSemantics` |
| 공간 부족 시 chunk를 교체해도 기존 예약이 살아 있다 (O1) | `TestChunkSwap` |
| 임계값 초과 요청은 current chunk를 교체하지 않는다 (O5) | `TestLargeAllocation` |
| 마지막 참조가 사라진 chunk가 pool로 돌아온다 (O2) | `TestPoolReturn` |
| 다른 thread에서 `SendView`를 놓아도 안전하다 (O1, O2) | `TestCrossThreadRelease` |
| `SendView` 복사본이 chunk 수명을 각자 붙잡는다 | `TestViewCopy` |
| 다중 thread 반복 할당/해제에서 corruption이 없다 | `TestStress` |
