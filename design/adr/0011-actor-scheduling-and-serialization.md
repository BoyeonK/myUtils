# ADR-0011 Actor 실행 직렬화와 스케줄링 모델

- Status: Accepted
- Date: 2026-09-15
- Follow-up: 2026-09-15 — 아래 "후속" 참고. 단일 global queue는 최종 구조가 아니다.
- 상세 규약: [design/actor-runtime.md](../actor-runtime.md) §2, §4, §5

## Context

> 어떻게 Actor 실행을 직렬화하고, Message와 runnable wakeup 유실을 막는가?

Actor 하나에 스레드 하나를 주지 않으면서 "동일 Actor는 동시에 하나의 worker에서만
실행된다"를 보장해야 한다. 그리고 그 보장을 만드는 과정에서 **유실 가능성이 두 층위에
생긴다.**

| | 유실되는 것 | 증상 |
|---|---|---|
| A | 메일박스에 메시지가 있는데 runnable 등록이 안 됨 | 그 액터만 멈춤 |
| B | runnable 큐에 ACB가 있는데 worker가 자고 있음 | 전체가 멈춤 |

둘은 같은 종류의 버그가 다른 층위에 나타난 것이다. 따로 풀면 한쪽을 빠뜨린다.

## Decision

- **Mailbox는 MPSC.** 1차 구현은 mutex + `deque`.
- **scheduling ownership은 atomic 상태 머신으로 관리한다.**
  `IDLE → SCHEDULED → RUNNING → IDLE`, `IDLE → SCHEDULED` CAS 성공자만 runnable 등록.
- **acceptance 직렬화에는 mailbox mutex를 쓴다**(`Send`/`Stop` 공유 게이트).
- **runnable을 만드는 모든 경로는 단일 함수 `EnqueueRunnable`을 거친다.**
  큐 삽입 → sleep 뮤텍스 handshake → notify가 그 안에서 원자적으로 묶인다.
- **1차 구현의 atomic ordering은 전부 `seq_cst`.**
- **runnable 단위는 메시지가 아니라 ACB.** 한 번 스케줄되면 최대 32건을 배치 처리한다.
- **`Actor::Handle()`은 mailbox 락을 보유한 채 호출하지 않는다.**

프로토콜 의사코드와 경합 분석은 [actor-runtime.md](../actor-runtime.md) §4, §5에 있다.

## 근거

### 왜 상태 머신이 atomic bool 여러 개보다 나은가

"실행 중인가"와 "큐에 있는가"를 별도 플래그로 두면 둘 사이의 중간 상태가 생기고,
전이마다 어떤 조합이 합법인지 따져야 한다. **단일 atomic enum + CAS**면 합법 전이가
표 하나로 닫히고, `IDLE → SCHEDULED` CAS가 runnable 큐 중복 등록을 구조적으로 막는다 —
큐에 같은 ACB가 두 번 들어갈 경로 자체가 없어진다.

### 왜 1차 mailbox가 mutex인가

구현체 선택 우선순위를 "검증된 라이브러리 → mutex 기반 → custom lock-free"로 두었고,
hand-written lock-free는 1차 범위에서 제외하기로 했다.

그런데 실무적으로 더 결정적인 이유가 있다. 프로토콜 A의 correctness 증명이
**"producer의 push와 worker의 재확인 사이에 happens-before가 있다"**에 의존하는데,
mutex면 그게 자명하다. lock-free 큐로 바꾸면 "비었다"의 의미를 그 자리에서 다시
증명해야 하고, 그건 1차 구현에서 감당할 위험이 아니다.

부수 효과로 **메시지 순서가 전역 FIFO로 확정**된다. ordering guarantee는 명시적으로
결정해야 하는 항목이었다. producer 간 순서까지 정의되므로 "global ordering에 의존하지 말라"는 주의가
1차에서는 필요 없다. 다만 **나중에 lock-free로 교체하면 이 보장이 사라지므로**,
프로토콜이 전역 순서에 의존하지 않도록 유지한다.

### 왜 `EnqueueRunnable` 단일 경로인가

프로토콜 B의 "빈 임계구역 handshake"는 **큐 삽입과 notify가 항상 함께 일어날 때만**
성립한다. 어디선가 큐에 직접 넣고 notify를 건너뛰면 그 경로에서만 worker가 깨지 않는다.

지금은 큐가 하나뿐이라 실수할 여지가 적지만, **worker-local deque와 work stealing을
넣는 순간 runnable 생성 경로가 늘어난다.** 그때 notifier를 우회하는 경로가 생기는 것이
이 설계의 가장 그럴듯한 회귀 시나리오다. 단일 함수로 강제해 둔다.

### 왜 `seq_cst`인가

correctness를 먼저 확보한다. acquire/release로 완화하는 것은 각 전이마다 어떤 쓰기가
어떤 읽기에 보여야 하는지를 개별 증명해야 하는 작업이고, 이득은 측정 전에는 알 수 없다.
[ADR-0003](0003-chunk-lifetime-shared-ptr.md)에서 `shared_ptr`을 택한 것과 같은 판단이다.

### 왜 배치 처리인가

메시지마다 스케줄하면 큐 연산이 메시지 수에 비례한다. ACB 단위로 스케줄하고 배치로
처리하면 큐 연산이 "메시지가 생긴 액터 수"에 비례한다. 대신 한 액터가 worker를
독점하지 않도록 budget(32건)으로 끊는다.

## 기각된 대안

| 대안 | 기각 사유 |
|---|---|
| hand-written lock-free MPSC | 1차 범위 제외. 프로토콜 A 증명이 훨씬 어려워진다 |
| atomic bool 조합 | 합법 상태 조합을 따로 관리해야 한다. 중복 등록 방지가 구조적이지 않다 |
| 메시지 단위 스케줄링 | 큐 연산이 메시지 수에 비례 |
| 1차부터 work stealing | 첫 구현의 위험은 throughput이 아니라 lifetime/전이 correctness다. 단일 큐로 semantics를 검증한 뒤 교체하는 편이 검증하기 쉽다 |
| 처음부터 relaxed ordering | 측정 없는 최적화 |

## Consequences

- **메시지마다 동적 할당이 발생한다.** `unique_ptr<Message>`의 대가다. hot path로
  확인되면 value/variant message, pooled message, inline type-erased 중에서 교체한다.
  그때 프로토콜 A/B는 영향받지 않는다 — 메일박스 원소 타입만 바뀐다.
- **mutex mailbox가 contention 지점이 될 수 있다.** 한 액터에 producer가 몰리는
  워크로드에서 드러난다. 측정 후 판단한다.
- work stealing을 넣을 때 **`EnqueueRunnable` 우회 경로를 만들지 않는 것**이 회귀
  방지의 핵심이다.

## Uncertainty

`seq_cst` 선택은 correctness 우선의 보수적 결정이며 **실제 성능 비용을 측정하지 않았다.**

**Revisit when:** profiling에서 Actor scheduling atomic이 유의미한 hot path로 나타날 경우.

budget 32건도 근거 있는 값이 아니라 출발점이다. 한 액터의 처리 지연과 worker 독점
사이의 균형을 측정한 뒤 조정한다.

## 후속 (2026-09-15)

두 가지를 정정한다. 결정이 바뀐 것이 아니라 **서술이 부정확했던 것**이다.

### 1. 단일 global queue는 최종 스케줄러 구조가 아니다

위 "기각된 대안"의 `1차부터 work stealing`은 **시점에 대한 기각이지 구조에 대한 기각이
아니다.** 목표 구조는 처음부터 이것이었다.

```
Global Injection Queue (MPMC) + Worker-local Work-Stealing Deque + Work Stealing
```

현재의 `_runnable`은 **baseline scheduler**이며, 2차에서 injection queue 역할로 축소되고
그 앞에 worker-local deque가 붙는다. 자료구조 셋(mailbox=MPSC, injection=MPMC,
local=work-stealing deque)의 역할 분리와 목표 worker loop는
[actor-runtime.md](../actor-runtime.md)의 "목표 스케줄러 구조" 절에 있다.

이 ADR이 그 구조를 적지 않았던 것은 **요구사항 원문을 삭제할 때 그 문서에만 있던
내용을 옮기지 못했기 때문이다.** 끊어진 링크만 확인하고 고유 정보를 확인하지 않았다.

### 2. `EnqueueRunnable` invariant의 정확한 형태

위 Decision의 "큐 삽입 → handshake → notify"는 큐가 단수라 **"모든 runnable은 global
queue로 간다"**로 읽힌다. 그런 뜻이 아니다.

> **모든 runnable 생성은 Scheduler의 scheduling entry point를 거치며,
> 어느 큐에 들어가든 필요한 wakeup notification이 함께 수행된다.**

2차에서 `EnqueueRunnable`은 locality를 보고 **local deque와 injection queue 중 어디에
넣을지 결정하는 단일 진입점**이 된다. 지켜야 할 것은 "한 곳에 모은다"가 아니라
"진입점을 우회하지 않는다"이다.

덧붙여, 큐마다 notify가 필요한 조건이 다르다는 점이 2차의 실제 난점이다 —
local push마다 깨우면 local deque를 둔 이득이 사라지고, 전혀 깨우지 않으면 한 worker에
일이 쌓이는 동안 나머지가 잔다. [actor-runtime.md](../actor-runtime.md)의
"2차에서 반드시 결정해야 할 것" 참고.
