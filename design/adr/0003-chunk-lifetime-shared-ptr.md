# ADR-0003 chunk 수명을 shared_ptr로 관리

- Status: Accepted
- Date: 2026-09-15

## Context

[ADR-0001](0001-refcounted-arena.md)에서 chunk 수명을 참조 카운트로 관리하기로 했다.
구현 수단이 두 가지다.

- `std::shared_ptr<SendBufferChunk>`
- chunk에 `std::atomic<uint32_t> _refCount`를 두는 intrusive 방식

성능이 중요한 코드에서는 `shared_ptr`의 control block 오버헤드를 피하는 게 정설이라,
처음에는 intrusive를 염두에 뒀다.

## Decision

**우선 `shared_ptr`을 쓴다.** 단, 타입 별칭으로 격리해 교체 비용을 낮춘다.

```cpp
using ChunkRef = std::shared_ptr<SendBufferChunk>;
```

`SendBuffer`/`SendView`/`SendBufferManager`가 `ChunkRef`만 알게 하면, 나중에
intrusive로 옮길 때 이 한 줄과 스마트 포인터 구현만 바꾸면 된다.

## 근거

control block 비용이 **조각마다가 아니라 chunk 수명마다** 발생한다는 점이 결정적이다.

```
16 KB chunk, 200 B 패킷  →  chunk 하나가 약 80개 패킷을 담는다
                         →  control block 할당은 패킷당 약 0.0125회
```

조각을 만들 때 드는 비용은 양쪽 모두 atomic 증가 1회로 동일하다. 차이는 chunk를
새로 만들 때의 할당 1회뿐이고, 그건 정의상 micro-optimization이다.

| | shared_ptr | intrusive |
|---|---|---|
| 조각 복제 비용 | atomic 증가 1회 | atomic 증가 1회 (동일) |
| chunk 획득당 추가 할당 | control block 1회 | 0회 |
| `ChunkRef` 크기 | 16 B | 8 B |
| 직접 작성할 코드 | 없음 | refcount + 스마트 포인터 래퍼 |
| 실수 여지 | 거의 없음 | AddRef/Release 누락, 메모리 순서 지정 오류 |

특히 메모리 순서가 중요하다. intrusive로 직접 짜면 감소를 `release`로 하고 0에
도달했을 때 `acquire` fence를 넣어야 하는데(그래야 마지막 releaser가 다른 스레드의
쓰기를 모두 보고 안전하게 `Reset`할 수 있다), 이걸 빠뜨리기 쉽다. 표준 라이브러리
구현은 이미 정확하다.

## Consequences

- `Reset()` 이후 다음 owner가 이전 `_offset` 값을 올바르게 보는 것은 `shared_ptr`의
  release/acquire와 ChunkPool 큐의 순서 보장이 함께 만든다.
- 교체 판단은 profiling 후에 한다. 판단 근거는 `SnapshotStats()`의 chunk 획득 빈도다.
- Pool 반환을 위해 custom deleter를 쓰므로 `make_shared`의 단일 할당 이점은 쓰지
  못한다. 이것도 chunk당 1회라 문제되지 않는다.
  ([ADR-0004](0004-single-global-chunkpool.md) 참고 — deleter는 pool 지분도 캡처한다)
