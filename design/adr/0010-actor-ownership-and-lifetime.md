# ADR-0010 Actor 소유권과 수명 모델

- Status: Accepted
- Date: 2026-09-15
- 상세 규약: [design/actor-runtime.md](../actor-runtime.md) §1

## Context

> 누가 Actor를 소유하고, 언제 Actor가 죽는가?

Actor Runtime에는 같은 `ActorControlBlock`(ACB)을 동시에 참조할 수 있는 주체가 셋
있다 — 외부 핸들(`ActorRef`), 스케줄러의 runnable 큐, 그리고 시스템 자신. 여기에
"Actor 본체는 죽었지만 ACB는 잠시 더 살아야 한다"는 요건이 겹친다.

수명 주체를 정하지 않으면 두 가지가 동시에 위험해진다 — 늦게 실행된 runnable이
이미 사라진 Actor를 건드리는 use-after-free, 그리고 아무도 모르게 Actor가 사라지는
조용한 유실.

## Decision

### 1. `ActorSystem`이 Registry를 소유하고, Registry가 Actor의 논리적 수명을 쥔다

```
Spawn → Alive → Stop() 또는 ActorSystem::Shutdown() → Dead
```

**마지막 `ActorRef`가 사라져도 Actor는 죽지 않는다.**

### 2. `ActorRef`의 수명과 Actor의 수명은 분리한다

`ActorRef`는 외부 접근 핸들일 뿐이다. 마지막 핸들이 사라졌다는 것은 "더 이상 외부가
접근하지 않는다"는 뜻이지 "종료하라"는 뜻이 아니다.

### 3. ACB가 Actor를 `unique_ptr`로 소유하고, `Spawn`도 `unique_ptr`을 받는다

```cpp
ActorRef Spawn(std::unique_ptr<Actor> actor);
```

### 4. ACB → Scheduler는 weak 참조다

## 근거

### 왜 Registry인가

Registry가 없으면 ACB의 수명은 "`ActorRef` + runnable 큐" 참조 수에 달린다. 그러면
**핸들을 버리는 순간 Actor가 메일박스에 메시지를 남긴 채 조용히 소멸**한다.

`spawn` 후 핸들을 지역 변수에 두고 스코프를 벗어나는 것은 흔한 코드다. 그게 곧
"액터 종료"를 뜻한다면 사용자는 반드시 한 번 당한다. Actor 모델의 통념과도 어긋난다 —
어느 구현에서도 핸들 수명이 액터 수명이 아니다.

대가는 Registry(뮤텍스 + 컨테이너)와 DEAD 액터 정리 로직이다. 예측 가능성을 위해
치를 만한 비용으로 봤다.

### 왜 `unique_ptr`이고, 특히 왜 `Spawn(unique_ptr)`인가

이게 이 ADR에서 가장 중요한 결정이다.

```cpp
ActorRef Spawn(std::shared_ptr<Actor> actor);   // 기각
```

이 형태면 호출자가 같은 `shared_ptr`을 계속 들고 있을 수 있고, 그러면 **런타임 밖에서
Actor state를 직접 수정할 수 있다.** **Actor 격리 불변식**("Actor의 mutable state는
handler 실행 중에만 변경한다")가 **API 수준에서 뚫린다.**

`unique_ptr`로 받으면 소유권이 넘어가는 순간 호출자에게 경로가 남지 않는다. 규율이
아니라 타입으로 강제된다.

부수 효과: 테스트에서 Actor 내부 상태를 직접 들여다볼 수 없다. 이건 문제가 아니라
의도다 — 상태는 메시지로 질의하거나, 생성 시점에 넘긴 외부 관측 객체에 기록한다.
그게 Actor 모델을 쓰는 방식이다.

### 왜 weak인가

Scheduler의 runnable 큐가 ACB를 strong으로 잡으므로 반대 방향이 strong이면 순환이
생긴다. weak이면 `ActorSystem`이 먼저 소멸해도 남은 `ActorRef`가 "이미 끝난 시스템"을
안전하게 판정할 수 있다. [ADR-0004](0004-single-global-chunkpool.md)에서 ChunkPool에
쓴 것과 같은 패턴이다.

## Consequences

- Actor를 종료시키는 방법은 **명시적 `Stop()`과 시스템 `Shutdown()` 둘뿐**이다.
  잊으면 시스템 수명 동안 남는다. 예측 가능하지만 누수처럼 보일 수 있다.
- ACB는 Finalize 이후에도 살아 있을 수 있다. 그 상태로 worker가 집어도 진입 CAS가
  실패해 no-op이 된다([actor-runtime.md](../actor-runtime.md) §7).
- Finalize가 Registry에서 자신을 제거하므로 **Registry를 순회하며 Finalize하면 안
  된다.** shutdown은 스냅샷을 만든 뒤 락 밖에서 처리한다([actor-runtime.md](../actor-runtime.md) §8).
- `Spawn`이 `unique_ptr`을 받으므로 호출자는 Actor를 미리 만들어 설정한 뒤 넘긴다.
  나중에 `Spawn<T>(args...)` 형태를 추가할 여지는 남아 있다.

## Uncertainty

Registry가 DEAD 액터를 즉시 제거하므로 누수는 없지만, **`Stop()`을 잊은 액터가 쌓이는
것을 감지할 수단이 없다.** 살아있는 액터 수 계측이 필요해지면 그때 추가한다.
