# ADR-0008 baseline 튜닝 정책과 allocator의 목적 정의

- Status: Accepted
- Date: 2026-09-15

## Context

`SendBuffer`의 정책 상수 세 개가 근거 없이 정한 출발점으로 남아 있었다(구 Q-001).
이 값들을 정하려면 **이 allocator가 무엇을 위한 것인지**를 먼저 정해야 한다.
같은 구조라도 용도에 따라 정반대 값이 최적이 되기 때문이다.

## Decision

### 이 allocator의 목적

범용 장기 송신 버퍼가 아니라, **UDP/TCP 기반 실시간 통신의 빈번한 I/O hot path에서
작은~중간 크기 송신 버퍼를 빠르게 할당하는 용도**로 한정한다.

우선순위는 이 순서다.

1. 빈번한 패킷 생성에서 chunk 획득/반환과 Current Chunk 교체 횟수를 충분히
   amortize한다.
2. 작은 패킷 여러 개를 하나의 chunk에서 연속적으로 bump allocation할 수 있게 한다.
3. **느린 peer가 `SendView`를 장시간 붙잡아 생기는 memory pinning 최소화는 이
   allocator의 주요 최적화 목표로 삼지 않는다.** 필요하면 상위 networking/session
   정책에서 별도로 대응한다.

### 값

```cpp
DEFAULT_CHUNK_CAPACITY     = 64 * 1024;
LARGE_ALLOCATION_THRESHOLD = 16 * 1024;
DEFAULT_POOL_IDLE_LIMIT    = 64;
```

## 근거

### 왜 64 KB인가

우선순위 1·2를 따르면 chunk는 클수록 좋고, 그걸 억제하던 유일한 근거가 pinning이었다.
우선순위 3에서 pinning을 목표에서 내렸으므로 16 KB를 고수할 이유가 없다.
64 KB면 200바이트 패킷 기준 약 320개를 한 chunk에서 처리한다.

### 왜 threshold를 capacity와 같이 두지 않는가

**두 값은 의미가 다르다.**

| 상수 | 의미 |
|---|---|
| `DEFAULT_CHUNK_CAPACITY` | 작은 할당을 많이 amortize하기 위한 **저장 공간 크기** |
| `LARGE_ALLOCATION_THRESHOLD` | 하나의 요청이 Current Chunk를 교체하게 만들 만큼 큰지를 판정하는 **정책값** |

같이 두면 20~30 KB 요청 하나가 64 KB Current Chunk를 갈아치우면서 남은 공간을 통째로
버린다. threshold를 16 KB로 낮추면 그런 요청은 전용 chunk로 빠지고, Current Chunk는
이후 작은 패킷들이 계속 쓴다.

### 왜 pool idle 64인가

64 KB × 64 = 약 **4 MB**의 유휴 용량을 캐싱하는 셈이다. 현재 서버 규모에서 합리적인
초기 메모리 예산으로 본다.

## 이 값들은 최적값이 아니다

**현재 workload 목표에 맞는 baseline policy를 확정하는 것**이지 최적값을 주장하는 게
아니다. 그래서 Q-001을 "측정 전에는 결정할 수 없는 질문"으로 계속 열어두지 않고 닫는다.
기본값 없이 열어두는 것보다, 근거를 명시한 기본값을 두고 문제가 관측될 때 다시 여는
편이 낫다.

## Revisit when

실제 네트워크 workload에서 다음 중 **하나라도** 관측되면 새 튜닝 질문을 연다.

| 관측 | 판정 수단 |
|---|---|
| chunk acquire/return 빈도가 여전히 지나치게 높음 | `chunkAcquireCount` |
| 큰 요청 때문에 chunk tail discard가 유의미하게 발생 | `discardedBytes` |
| large allocation 경로가 예상보다 자주 사용됨 | `largeAllocCount` |
| live chunk가 서버 memory budget에 유의미한 압력 | `LiveChunkCount()` |
| pool miss 또는 idle retained memory가 실제 문제 | `chunkCreateCount / chunkAcquireCount` |

## Consequences

- **[ADR-0001](0001-refcounted-arena.md)의 Consequences에 적힌 "pinning이 chunk 크기
  선택의 주된 제약"은 더 이상 유효하지 않다.** 그 서술은 당시 판단이었고, 여기서
  우선순위 3으로 명시적으로 뒤집었다. ADR-0001의 arena 설계 자체는 그대로다.
- 우선순위 3은 [ADR-0007](0007-no-standalone-path-for-pinning.md)이 standalone 경로를
  기각한 것과 같은 입장이다. 두 문서는 서로를 보강한다.
- threshold < capacity라서 **16~64 KB 요청이 전부 전용 chunk로 간다.** 전용 chunk는
  pool에 들어가지 않으므로 그만큼 malloc/free 쌍이 발생한다. 이 크기대 요청이 잦은
  workload라면 `largeAllocCount`가 먼저 신호를 준다.
- `LiveChunkCount()` × `DEFAULT_CHUNK_CAPACITY`로 점유 메모리를 추정하는 방식이
  **덜 정확해졌다.** 전용 chunk는 용량이 제각각인데 카운트에는 함께 잡히기 때문이다.
  위 revisit 조건 4번을 정밀하게 보려면 개수가 아니라 바이트를 세는 지표가 필요하다.
