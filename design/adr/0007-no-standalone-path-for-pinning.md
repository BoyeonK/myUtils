# ADR-0007 브로드캐스트·coalescing pinning에 standalone 경로를 두지 않는다

- Status: Accepted
- Date: 2026-09-15

## Context

chunk는 거기서 잘라낸 조각이 **전부** 해제돼야 pool로 반납된다
([ADR-0001](0001-refcounted-arena.md)). `SendView`를 복사 가능하게 만든 결정
([ADR-0002](0002-sendbuffer-sendview-split.md))이 브로드캐스트와 scatter/gather를
공짜로 지원해 주는 대신, 이 고정(pinning)을 두 방향으로 증폭시킨다.

- **브로드캐스트** — 한 패킷을 1000명에게 보내면 **가장 느린 한 명**이 완료될 때까지
  chunk가 고정된다. 그 chunk에 함께 들어 있던 다른 패킷들의 공간도 같이 묶인다.
- **coalescing** — 세션이 전송 대기열에 50개를 쌓으면 최대 50개 chunk를 붙잡는다.

최악의 경우 200바이트 패킷 하나가 16 KB를 묶는다. 대응한다면 그 패킷을 정확한
크기의 standalone 버퍼로 빼서 200바이트만 고정되게 할 수 있다.

## Decision

**standalone 경로를 지금 도입하지 않는다.** 현재 구조를 유지한다.

## 근거

- **chunk pinning이 실제로 memory pressure를 만든다는 측정 결과가 없다.** 지금까지
  관찰된 수치는 전부 합성 테스트에서 나온 것이라 근거가 되지 않는다.
- **별도 경로를 추가하면 API와 할당 정책이 복잡해진다.** 아래 두 선택지 모두 호출자가
  fanout을 미리 판단해 알려주어야 하고, 그 판단이 틀리면 조용히 손해가 난다.

## 기각된 대안

| 안 | 내용 | 기각 사유 |
|---|---|---|
| B | `Reserve(size, Lifetime::Long)` 힌트 추가 | API 표면이 늘고 정책 판단이 호출자 전체로 분산된다 |
| C | 별도 함수 `ReserveStandalone(size)` | 진입점이 둘로 늘어난다. [ADR-0005](0005-reserve-commit-tail-reclaim.md)에서 같은 이유로 한 번 기각한 방향이다 |

둘 다 **기술적으로 어렵지 않다.** 기각 사유는 난이도가 아니라 "아직 풀어야 할
문제인지 확인되지 않았다"는 것이다.

## Revisit when

대표 워크로드에서 다음 중 **하나라도** 관측되면 다시 검토한다.

1. chunk pinning으로 인한 절대 메모리 사용량이 서버 memory budget에 유의미한
   영향을 준다
2. 장수 `SendView` 때문에 다수의 chunk가 장시간 반환되지 않는다
3. chunk pinning 때문에 pool miss / chunk allocation churn이 증가한다

## Consequences

- 200바이트 패킷이 16 KB를 묶는 최악의 경우가 그대로 남는다. 이건 알고 받아들이는
  비용이며, 완화 수단은 chunk 크기 조정뿐이다(Q-001).
- **재검토 조건 2번은 현재 관측할 수단이 없다.** 1번과 3번은 기존 계측으로 판단할 수
  있지만(아래 표), 2번은 chunk가 얼마나 오래 고정되어 있는지를 봐야 하는데 그 지표가
  없다. 이 조건을 실제로 쓰려면 계측을 먼저 추가해야 한다.

| 조건 | 관측 수단 | 지금 가능한가 |
|---|---|---|
| 1. 절대 메모리 | `LiveChunkCount()` × `DEFAULT_CHUNK_CAPACITY` | 가능 |
| 2. 장시간 미반환 | chunk 생존 시간 또는 세션당 pending `SendView` 수 | **없음** |
| 3. churn 증가 | `chunkCreateCount / chunkAcquireCount` | 가능 |
