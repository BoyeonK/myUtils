# ADR-0002 SendBuffer / SendView 타입 분리

- Status: Accepted
- Date: 2026-09-15

## Context

이 API는 세 가지 방식으로 잘못 쓰이기 쉽다.

1. 크기를 확정하지 않은 채 전송한다
2. 확정한 뒤에도 버퍼에 계속 쓴다 (다음 할당 영역을 침범한다)
3. I/O가 완료되기 전에 버퍼를 놓는다 (커널이 읽고 있는 메모리가 재사용된다)

셋 다 런타임에야 드러나고, 3번은 재현조차 어렵다. 한편 게임 서버에서는 한 패킷을
N개 세션에 브로드캐스트하는 패턴이 흔해서 **공유 가능한 형태도 필요**하다.

이 둘은 상충하는 것처럼 보인다. 쓰기 중에는 배타적이어야 하고, 전송 중에는 공유
가능해야 한다.

## Decision

**상태를 타입으로 나눈다.** 하나의 타입에 두 성질을 넣지 않는다.

```
SendBuffer   이동 전용, mutable    "아직 작성 중"
     │
     │  Commit(actualSize) &&      rvalue 한정 — 원본을 소비한다
     ▼
SendView     복사 가능, immutable  "작성 완료, 전송용"
```

- `Commit`이 `SendBuffer`를 소비하므로 **확정 후 쓰기가 타입 수준에서 불가능**하다.
- `AsyncSend`가 `SendView`만 받으므로 **확정 없이 전송하는 것도 불가능**하다.
- `SendView` 복사는 chunk refcount 증가 하나뿐이라 브로드캐스트가 자연스럽다.

조기 해제(3번)는 `AsyncSend`가 `SendView`를 **값으로 받아 completion context에
저장**하는 것으로 막는다. 호출자에게는 사본만 남고 실제 수명은 I/O 계층이 쥔다.
IOCP라면 per-operation 구조체에 멤버로 두고, 그 구조체가 파괴되는 시점이 곧 해제다.

## 기각된 대안

### 단일 타입 + `_committed` 플래그

런타임 검사로 떨어진다. 타입이 막아줄 수 있는 것을 assert로 막는 셈이고,
"확정 전 사본"이라는 의미가 모호한 상태가 생긴다.

### 항상 `shared_ptr<SendBuffer>`로 반환

기존 구현이 이 방식이었다. 조각 하나마다 control block을 힙에 할당하므로
**패킷당 malloc이 1회** 발생한다. 풀을 도입한 이유를 상당 부분 상쇄한다.
공유가 필요한 경우에만 호출자가 `make_shared<SendView>(...)`로 감싸면 된다.

## Consequences

- 조각 발급에 할당이 없다. `SendBuffer`/`SendView` 모두 스택 위의 값 타입이다.
- **scatter/gather send가 공짜로 따라온다.** 서로 다른 chunk에서 나온 여러
  `SendView`를 `WSABUF` 배열로 묶어 한 번의 `WSASend`에 넘길 수 있고, 각 View가
  자기 chunk를 독립적으로 살려둔다. 세션의 전송 대기열을 `std::vector<SendView>`로
  두면 그대로 동작한다.
- 대가는 pinning 증폭이다. 브로드캐스트는 1000명 중 가장 느린 한 명이 완료될
  때까지 chunk를 붙잡고, coalescing은 대기열에 쌓인 조각 수만큼 chunk를 붙잡는다.
  chunk 크기를 정할 때 두 경로를 함께 계산해야 한다.
- `SendView::Data()`는 `const std::byte*`다. `WSABUF::buf`가 non-const `CHAR*`라서
  I/O 어댑터에서 `const_cast`가 한 번 필요한데, **그 캐스트를 경계 한 곳에 가두고
  그 위로는 const를 유지**한다.
- `Commit`은 rvalue 한정이라 lvalue 오용을 막지만 `std::move`를 두 번 쓰는 것까지
  막지는 못한다. 그래서 `Commit`이 자기 `ChunkRef`를 넘기면서 **원본을 비운다.**
  두 번째 호출은 무효 버퍼 접근으로 걸린다.
