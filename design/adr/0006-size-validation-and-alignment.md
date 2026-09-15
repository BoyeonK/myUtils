# ADR-0006 크기 검증 계층, 실패 정책, alignment 제외

- Status: Accepted
- Date: 2026-09-15

## Context

두 가지를 정해야 했다.

1. 외부 입력에서 온 크기를 **어느 계층이 검증**하는가, 그리고 실패를 어떻게 알리는가
2. 할당에 **정렬 보장**을 넣을 것인가

## Decision 1: 크기 검증은 두 계층 모두가 책임진다

목적이 서로 다르므로 중복이 아니다.

| 계층 | 묻는 질문 | 초과 시 |
|---|---|---|
| 프로토콜 | 이 프로토콜에서 이 크기가 정상인가? | 세션/요청 실패 |
| SendBuffer | 우리 메모리 시스템이 이 크기를 허용하는가? | 무효 `SendBuffer` 반환 |

프로토콜을 거치지 않는 내부 경로(서버가 생성하는 브로드캐스트 페이로드 등)가 있는
한, 메모리 계층의 절대 상한(`MAX_SEND_BUFFER_SIZE`)은 독립적으로 필요하다.

`DEFAULT_POOL_IDLE_LIMIT`과는 전혀 다른 값이다. 전자는 "한 번의 send가 얼마나 큰
메모리를 요구할 수 있는가", 후자는 "사용이 끝난 chunk를 몇 개까지 캐싱할 것인가"다.

### 실패 정책

```
정상 범위          →  할당
절대 상한 초과      →  무효 SendBuffer 반환   (soft failure, 호출자가 처리)
반환값 미확인 사용  →  ASSERT_CRASH           (계약 위반, 프로그래밍 오류)
std::bad_alloc     →  그대로 전파
내부 불변식 붕괴    →  ASSERT_CRASH
```

두 단계로 나눈 이유가 있다. 절대 상한은 정의상 "프로토콜 계층이 제 일을 했다면
발생하지 않는" 사건이라, 호출자들이 반환값 검사를 생략하기 딱 좋다. 그래서
**실패는 반환값으로 주되, 무시하면 조용한 null 역참조가 아니라 확정적인 크래시**가
되게 한다. `WritableData()`와 `Commit()`이 무효 버퍼를 `ASSERT_CRASH`로 잡는다.

## Decision 2: alignment 파라미터를 API에 넣지 않는다

`Reserve(size)`만 둔다. 커서는 정렬 올림 없이 정확히 요청한 만큼만 전진한다.

### 근거

처음에는 "`reinterpret_cast<PacketHeader*>(buffer)`로 쓰이니 비정렬 접근이 된다"는
이유로 8바이트 정렬을 넣으려 했다. **이 근거는 틀렸다.** `reinterpret_cast`는 raw
byte 위에 객체를 **생성하지 않는다**. 그러니 그것이 정렬의 근거가 될 수 없다.

실제 기준은 이것이다.

| 사용 방식 | 정렬 |
|---|---|
| `memcpy` / `WriteUInt32` 등 byte stream으로 취급 | 불필요 |
| storage 위에 실제로 typed object를 생성 (placement new) | `alignof(T)` 필요 |

`SendBuffer`의 목적을 **byte serialization storage로 한정**했으므로 현재는 불필요하다.

opt-in 파라미터로 열어두는 것도 기각했다. API에 있으면 쓰이고, 쓰이면 정렬 규율이
호출자 전체로 분산된다. 누군가 `alignof(T)`를 빠뜨려도 x86에서는 조용히 동작하다
다른 환경에서 터진다.

## Consequences

- 정말 storage 위에 객체를 생성해야 하는 요구가 생기면 그때 `Reserve(size, alignment)`
  오버로드를 추가한다. 나중에 넓히는 건 쉽고 좁히는 건 어렵다.
- `ASSERT_CRASH`에는 `NDEBUG` 가드가 없어 릴리즈에서도 죽는다. 그래서 복구 가능한
  실패에는 절대 쓰지 않는다 — 그런 건 반환값으로 알린다.
- `Reserve(0)`은 무효 `SendBuffer`, `Commit(0)`은 유효한 크기 0짜리 `SendView`다.
  전자는 의미 없는 요청이고 후자는 정상적인 빈 전송이라 구분했다.
