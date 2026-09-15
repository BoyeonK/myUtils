# ADR-0012 Actor 종료·실패·셧다운 semantics

- Status: Accepted
- Date: 2026-09-15
- 상세 규약: [design/actor-runtime.md](../actor-runtime.md) §3, §6, §7, §8

## Context

> Actor / Runtime이 종료되거나 실패할 때 어떤 Message를 처리하고, 누가 cleanup하며,
> 어떤 lifetime guarantee를 제공하는가?

정상 경로보다 이쪽이 어렵다. `Send`와 `Stop`이 경합하고, Finalize 진입 후보가 넷이며,
shutdown 시점에 Registry·runnable 큐·외부 `ActorRef`가 동시에 같은 ACB를 참조한다.
게다가 user code(`Handle`, 메시지 소멸자, Actor 소멸자)가 이 경로 한가운데에서 실행되고
**다시 런타임 API를 호출할 수 있다.**

## Decision

### 1. `Send`와 `Stop`은 mailbox mutex를 acceptance boundary로 공유한다

Stop은 락 안에서 **CAS 루프로 이전 상태를 원자적으로 포착**하고, 그 값으로 Finalize
주체를 정한다(IDLE이면 호출자, SCHEDULED/RUNNING이면 worker).

### 2. `Send() == true`는 accepted만 의미한다

delivery guarantee가 아니다. 이후 `Stop`/`Shutdown`이 그 메시지를 버릴 수 있다.
실패는 1차 구현에서 `bool`로 표현한다.

### 3. `Stop()`은 즉시 중단이며 pending 메시지를 버린다

graceful stop이 아니다. 실행 중인 handler만 완료된다.

### 4. Handler 예외는 해당 Actor를 Stop시키고 **drain 루프를 즉시 탈출**한다

### 5. Finalize는 once gate로 정확히 한 번만 실행된다

### 6. 락 규칙 셋을 강제한다

- `Actor::Handle()`은 mailbox 락을 보유한 채 호출하지 않는다
- registry 락과 mailbox 락을 중첩하지 않는다
- 메시지 파괴는 mailbox 락 밖에서 한다

### 7. Shutdown은 worker join 이후 Registry 스냅샷을 만들어 락 밖에서 Finalize한다

### 8. 시스템 게이트는 best-effort, 액터 게이트는 선형화 보장이다

### 9. `workerCount >= 1`을 강제한다

## 근거

### 왜 mailbox mutex가 acceptance gate인가

`Send`와 `Stop`이 서로 다른 동기화 수단을 쓰면 "Stop 이후에 accept되는 메시지"가
존재할 수 있는지 판정할 수 없다. 같은 락 안에서 상태를 읽고 쓰면 모든 경합이
**"누가 뮤텍스를 먼저 잡았는가"로 환원**되고, 그 외의 배치가 존재하지 않는다.

`prev`를 CAS 루프로 잡는 이유는 따로 있다. `IDLE → SCHEDULED` 전이는 mailbox 락
**밖에서** 일어나므로, 단순히 `prev = load(); store(STOPPING)`으로 쓰면 그 사이에
producer가 SCHEDULED로 바꾸고 큐에 등록할 수 있다. 그러면 Stop 호출자는 `prev == IDLE`로
믿고 Finalize하는데 ACB는 이미 큐에 들어가 있다. once gate와 진입 CAS 덕분에 UAF로는
번지지 않지만 주체 판정이 틀린다. **락은 acceptance를 선형화하고, CAS 루프는 락 밖
전이와의 경합을 막는다. 둘 다 필요하다.**

### 왜 `Stop`이 graceful이 아닌가

drain 의미를 섞으면 "언제 끝나는가"가 액터가 스스로에게 보내는 메시지에 의존하게 된다.
자기 자신에게 계속 메시지를 보내는 액터는 영원히 종료되지 않는다.

**즉시 중단은 종료가 보장되는 정책**이고, 그게 1차에 필요한 성질이다. graceful이
필요해지면 `StopAfterDrain()`을 별도 API로 추가한다 — 하나의 `Stop`에 두 정책을 섞으면
정책이 섞여 일관성이 깨진다.

대가는 명확하다. **"마지막 저장 메시지를 보내고 Stop"이 조용히 실패한다.** 이름과 기대가
어긋나기 쉬운 지점이라 문서에 못박는다.

### 왜 예외 시 Actor를 Stop하는가

handler가 중간에 던졌다면 Actor 내부 state가 **부분적으로만 변경**되었을 수 있다.

```cpp
hp -= damage;
UpdateInventory();     // throw
UpdateStateMachine();  // 실행 안 됨
```

이 상태로 다음 메시지를 계속 처리하는 것이 가장 위험하다. supervision/restart가 없는
1차 구현에서는 **격리가 가장 예측 가능**하다.

`break`가 필수인 이유는 Stop이 state만 바꿀 뿐 worker는 여전히 drain 루프 안이기
때문이다. 탈출을 명시하지 않으면 다음 메시지를 계속 처리한다 — 격리하려던 목적과
정반대다.

예외를 잡지 않는 선택지는 배제했다. 전이 도중에 스택을 빠져나가면 상태 머신이
중간 상태로 멈춰 다른 액터까지 영향을 받는다.

### 왜 once gate인가

Finalize 진입 후보가 넷이다 — IDLE 상태의 Stop 호출자, SCHEDULED를 집은 worker,
RUNNING worker, shutdown cleanup. 상태 머신만으로도 중복을 막을 수 있지만,
**lifetime 코드는 "논리적으로 불가능하다"에 기대지 않는 편이 낫다.** 전이를 하나
추가하는 순간 그 증명이 조용히 깨진다.

### 왜 락 규칙이 ADR에 들어가는가

세 규칙 모두 **user code가 런타임 API를 재호출할 수 있다**는 하나의 사실에서 나온다.

| 규칙 | 막는 것 |
|---|---|
| `Handle` 중 mailbox 락 금지 | self-send, self-stop 데드락 |
| 메시지 파괴를 락 밖에서 | 메시지 소멸자의 `Send` 재진입 |
| registry/mailbox 락 중첩 금지 | Finalize의 unregister와 lock ordering 충돌 |

특히 첫 번째는 **"배치 처리니까 락을 한 번만 잡자"는 최적화로 자연스럽게 들어올 수
있다.** 그래서 관례가 아니라 결정으로 남긴다. 어겼을 때 관측되는 결과를 테스트로
고정해 뒀다(`SelfSendFromHandler` 등).

### 왜 shutdown이 스냅샷을 만드는가

Finalize가 Registry unregister를 하므로, Registry를 순회하며 Finalize하면 같은 뮤텍스
재획득(데드락) 또는 iterator 무효화가 난다. 스냅샷을 만들고 락 밖에서 Finalize하면
Finalize 중 unregister가 일어나도 안전하다.

worker join **이후에** 하는 것도 중요하다. 그 시점엔 worker가 더 이상 Actor를 실행하지
않으므로 Finalize가 단순해진다.

### 왜 두 게이트의 강도가 다른가

액터 단위 Stop은 mailbox 락으로 선형화된다. 반면 시스템 shutdown 플래그 확인은
mailbox 락 밖이라, 경합한 `Send`가 `true`를 받은 뒤 shutdown 5단계에서 discard될 수
있다. 이를 선형화하려면 모든 `Send`가 시스템 수준 락을 거쳐야 하는데, **전역 직렬화
지점을 hot path에 만드는 비용이 얻는 것보다 크다.**

best-effort로 두어도 `Send(true) = accepted, not delivered` 계약과 일관된다.
다만 **강도가 다르다는 사실 자체를 문서에 적어야** 한다.

## Consequences

- `Stop()` 이후 pending 메시지는 사라진다. graceful 종료가 필요한 프로토콜은 액터
  스스로 "이제 끝내도 된다" 시점을 판단해 `Stop`을 호출하도록 설계해야 한다.
- 예외를 던진 액터는 복구되지 않는다. 재시작이 필요하면 상위에서 새로 `Spawn`한다.
- handler가 무한정 block하면 shutdown의 worker join도 끝나지 않는다. 설계가 감수하는
  제약이다.
- 락 규칙 셋은 **성능 최적화가 정면으로 충돌하는 지점**이다. 배치 처리 최적화를
  검토할 때 반드시 이 ADR을 먼저 읽어야 한다.

## Uncertainty

`Send` 실패를 `bool`로만 표현하므로 **호출자가 실패 원인을 구분할 수 없다** — 액터가
죽었는지, 시스템이 내려갔는지 알 수 없다.

현재 원인별로 다르게 행동할 요구가 없어 `SendResult` enum까지 도입하지 않았다. 진단이
필요해지면 API 변경 없이 내부 계측(`rejectedByActorStop`, `rejectedBySystemShutdown`)으로
먼저 붙일 수 있다.

**Revisit when:** 호출자가 실패 원인을 구분해 다르게 동작해야 하는 실제 요구가 생길 경우.
