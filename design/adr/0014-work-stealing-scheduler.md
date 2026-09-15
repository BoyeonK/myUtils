# ADR-0014 2차 스케줄러 — work-stealing deque와 큐별 알림 정책

- Status: Accepted
- Date: 2026-09-15
- 관련: [ADR-0011](0011-actor-scheduling-and-serialization.md)의 "후속" 절이 목표 구조를 예고했다
- 상세 규약: [design/actor-runtime.md](../actor-runtime.md) §5, "목표 스케줄러 구조"

## Context

> 단일 global runnable queue를 worker-local work-stealing deque + global injection
> queue로 확장할 때, 무엇을 어느 큐에 넣고 언제 worker를 깨우는가?

1차 구현의 스케줄러는 **baseline**이었다. 단일 MPMC 큐 하나에 모든 runnable이 들어가고,
`EnqueueRunnable`이 큐 삽입 → sleep 뮤텍스 handshake → `notify_one()`을 항상 함께
수행해 lost wakeup을 막았다. correctness를 먼저 검증하려는 의도적 단순화였고
(ADR-0011), 목표 구조는 처음부터 아래였다.

```
                    Global Injection Queue (MPMC)
                            │
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
           Worker 0      Worker 1      Worker 2
           Local Deque   Local Deque   Local Deque
              ↕             ↕             ↕
               ─────── Work Stealing ───────
```

확장하면서 1차에 없던 판단이 셋 생긴다.

1. **어느 큐에 넣는가** — locality를 어떻게 판정하는가
2. **언제 깨우는가** — 큐마다 notify의 성격이 다르다 (Q-008)
3. **어느 순서로 찾는가** — local 우선이면 injection이 굶는다 (Q-009)

## Decision

### 1. local deque는 worker당 `std::mutex` + `std::deque`로 만든다

owner는 `push_bottom` / `pop_bottom`(LIFO), thief는 `steal_top`(FIFO). 전부 그 worker의
뮤텍스 아래에서 수행한다.

### 2. 라우팅은 thread_local `{ActorRuntime*, index}`로 판정한다

worker loop가 **진입할 때 스스로 RAII로 세팅**하고 나갈 때 지운다. `EnqueueRunnable`은
`tls.runtime == this`일 때만 그 worker의 local deque에 넣고, 아니면 injection queue로
보낸다.

**`tls.runtime`을 함께 확인하는 것이 필수다.** 인덱스만 두면 `ActorSystem`이 둘 이상일 때
시스템 A의 worker가 만든 시스템 B의 runnable이 A의 local deque로 들어간다.

### 3. notify 정책은 큐마다 다르다 (Q-008 → C)

| 큐 | notify | 성격 |
|---|---|---|
| Global injection | **항상** | **correctness.** 빠뜨리면 시스템이 멈춘다 |
| Local deque | **유휴 worker가 있을 때만** | **heuristic.** 빠뜨려도 진행은 보장된다 |

유휴 카운터 `_idleCount`는 **`_sleepLock` 안에서 증감한다.**

### 4. sleep 직전 재확인은 세 곳을 모두 본다

자기 local deque, injection queue, **그리고 다른 worker의 deque까지.**
1차와 마찬가지로 이 재확인은 `_sleepLock`을 **보유한 채** 수행한다.

### 5. worker loop는 61회마다 injection을 먼저 본다 (Q-009 → B)

```
매 61번째 반복  → injection 먼저, 없으면 local
그 외           → local 먼저, 없으면 injection
둘 다 없으면     → steal 시도 (임의의 victim부터 순회)
그래도 없으면    → sleep
```

### 6. `EnqueueRunnable`은 여전히 단일 진입점이다

큐가 둘이 되어도 **어느 큐에 넣을지 고르는 일까지 이 함수가 한다.** 큐를 직접
건드리는 경로를 만들지 않는다.

### 7. atomic ordering은 계속 `seq_cst`다

## 근거

### 왜 mutex deque인가 (Chase-Lev가 아니라)

ADR-0011이 구현체 선택 우선순위를 **"검증된 라이브러리 → mutex 기반 → custom
lock-free"**로 정해뒀다. Chase-Lev는 정확히 세 번째 칸이고, 이 구조를 도입하는 목적인
중앙 큐 경합이 실제로 병목이라는 **측정치가 아직 없다.**

그리고 mutex로도 목적의 대부분이 달성된다. **락이 1개에서 N개로 쪼개지는 것 자체가
경합 제거의 본체**이고, Chase-Lev가 추가로 버는 것은 "경합 없는 owner 연산"뿐이다.
스케줄러 연산은 이미 배치(32건)당 1회이므로 메시지당으로 환산하면 뮤텍스 비용이 묻힌다.

Chase-Lev의 비용은 낮지 않다. `bottom` 저장과 `top` CAS의 memory ordering 증명이
까다롭고 — 원 논문의 알고리즘에도 버그가 있었다 — 그 증명은 우리가 직접 져야 한다.

### 왜 두 큐의 notify 정책이 다른가

**같은 정책을 쓸 이유가 없다. 빠뜨렸을 때 잃는 것이 다르기 때문이다.**

injection의 notify를 빠뜨리면 producer가 외부 스레드일 수 있고 worker 전부가 자고
있을 수 있으므로 **아무도 깨지 않는다.** 시스템이 멈춘다. 이건 프로토콜 B가 막으려던
바로 그 상태다.

local push의 notify를 빠뜨리면 아무 일도 없다. push한 주체가 그 deque의 owner이고,
owner는 잠들기 전에 반드시 자기 deque를 확인하므로 **결국 자기가 처리한다.** 잃는
것은 "다른 worker가 대신 해줄 수도 있었다"뿐이다. 진행 보장이 아니라 부하 분산이다.

그래서 correctness가 걸린 쪽은 무조건 수행하고, heuristic인 쪽에만 조건을 붙인다.

### 왜 `_idleCount`를 `_sleepLock` 안에서 세는가

락 밖에서 세면 창이 생긴다 — producer가 0을 읽고 notify를 건너뛴 **직후** worker가
잠드는 경우다.

락 안에서 증가시키면 **"producer가 0을 읽었다"가 "sleep 영역에 들어와 있는 worker가
없다"와 같은 뜻**이 된다. 그 창에 진입하는 worker는 `_sleepLock`을 잡아야 하고, 잡은
뒤 반드시 §4의 재확인을 수행하므로 방금 push된 일을 본다.

### 왜 재확인이 다른 worker의 deque까지 보는가

§3의 heuristic에 남는 잔여 창을 0으로 만든다. 다른 worker의 deque를 보지 않으면,
"A가 자기 deque에 push(유휴 0이라 notify 생략) → B가 잠들며 재확인 → 자기 deque와
injection만 비어 있음을 확인하고 수면"이 성립한다. A가 긴 handler에 묶이면 그 일은
A가 풀려날 때까지 멈춘다.

비용은 **잠들기 직전에만** 드는 O(worker 수) 스캔이다. hot path가 아니다.

### 왜 61회마다 injection을 먼저 보는가

local 우선을 끝까지 밀면 **local deque가 계속 차 있는 동안 injection을 한 번도 보지
않는다.** local을 채우는 것은 handler가 만든 runnable(액터 간 메시지, self-send)이고,
이게 꾸준한 워크로드에서는 외부 스레드·I/O completion의 runnable이 무한정 밀린다.
게임 서버에서는 I/O 응답 지연으로 나타난다.

거꾸로 injection을 항상 먼저 보면 매 반복마다 중앙 MPMC 큐를 건드려 local deque를
도입한 이유를 깎는다.

**61은 최적값이 아니라 baseline이다.** ADR-0008이 SendBuffer 상수에 쓴 것과 같은
태도다 — 근거 있는 출발점을 정하고 관측되면 다시 연다.

### 왜 TLS에 runtime 포인터까지 넣는가

`ActorSystem`은 여러 개 존재할 수 있고 실제로 테스트가 그렇게 쓴다. 인덱스만 두면
시스템 A의 worker가 handler 안에서 시스템 B의 액터에게 `Send`할 때, B의 runnable이
**A의 local deque**로 들어간다. A의 worker가 B의 ACB를 실행하고, B의 `Shutdown`은
그 runnable을 회수하지 못한다.

인덱스는 "몇 번째 worker인가"만 말하고 "누구의 worker인가"를 말하지 않는다.
**두 값이 함께 있어야 의미가 닫힌다.**

### 이것이 ADR-0009와 충돌하지 않는 이유

ADR-0009는 `WorkerContext`를 도입하지 않기로 했고, `CLAUDE.md`는 "외부에서 배정받는
스레드 식별자에 의존하지 않는다"를 권고로 두고 있다. 과거 `MyThreadID`가 그런
값이었고 `InitTLS()`를 거쳐야만 유효했다.

여기서 쓰는 TLS는 성격이 다르다.

| | 과거 `MyThreadID` | 여기 |
|---|---|---|
| 초기화 주체 | 외부 `InitTLS()` 호출 | **worker loop가 진입 시 스스로** |
| 안 거치면 | 값이 무효. 쓰면 깨진다 | null. injection으로 떨어진다 |
| 범위 | 전역 `thread_local` | 런타임 내부, 특정 런타임에만 유효 |

**"쓰기 전에 초기화 훅을 불러야 한다"는 계약이 생기지 않는다.** worker가 아닌
스레드에서 `Send`하면 자연히 injection으로 가고, 그것이 정확히 스펙이 정한
"특정 worker와 연고가 없는 runnable" 처리다.

## 기각된 대안

| 대안 | 기각 사유 |
|---|---|
| Chase-Lev lock-free deque | ADR-0011의 구현체 우선순위 세 번째 칸. 경합이 병목이라는 측정치가 없다. 경합 제거의 본체는 락을 N개로 쪼개는 것이고 나머지는 증명 비용에 비해 이득이 작다 |
| local deque에도 moodycamel | `steal_top` / owner LIFO 의미가 없다. 자료구조의 역할이 다르다 |
| local push에 notify 안 함 (Q-008 A) | 한 worker가 긴 handler에 묶인 동안 그 deque의 일이 통째로 멈추는 것을 방치한다 |
| local push마다 notify (Q-008 B) | 중앙 sleep 뮤텍스를 push마다 건드려 local deque를 둔 이유가 사라진다 |
| `empty → non-empty` 전이에서만 notify (Q-008 D) | backlog 신호로 쓸 수 없다. 첫 push 이후로는 아무리 쌓여도 알리지 않는다 |
| local이 빌 때만 injection 확인 (Q-009 A) | injection 기아를 방치한다 |
| injection을 항상 먼저 (Q-009 C) | 매 반복 중앙 큐 접근. local deque의 이득을 깎는다 |
| TLS에 인덱스만 저장 | 다중 `ActorSystem`에서 runnable이 남의 런타임 deque로 샌다 |

## Consequences

- **`EnqueueRunnable`이 정책 함수가 되었다.** 이제 "넣고 알린다"가 아니라 "고르고
  넣고 조건에 따라 알린다"이다. 큐를 직접 건드리는 경로를 추가하면 그 경로에서만
  worker가 깨지 않는 회귀가 난다 — 1차와 같은 규칙이 더 중요해졌다.
- **프로토콜 B는 구조가 바뀌지 않았다.** 빈 임계구역 handshake와 "재확인은 sleep 락
  보유 중에" 규칙이 그대로다. 넓어진 것은 **"큐가 비었나"의 정의**뿐이다.
- **잠들기 비용이 O(worker 수)로 늘었다.** worker 수가 커지면 재검토 대상이다.
- **`61`과 batch budget `32`가 상호작용한다.** 한 번의 local 처리가 최대 32건이므로
  injection 확인 간격은 최악의 경우 메시지 1,952건이다. 둘 중 하나를 바꾸면 다른
  쪽의 의미도 바뀐다.
- 계측이 늘었다. `localPushCount` / `injectionPushCount`(라우팅이 도는가),
  `stealAttemptCount` / `stealSuccessCount`(stealing이 값을 하는가),
  `notifySkippedCount`(§3 정책이 실제로 아낀 것이 있는가). 전부 배치당이라 2단이다
  (ADR-0013).

## Uncertainty

**`61`도 `32`도 측정에서 나온 값이 아니다.** 근거 있는 baseline일 뿐이다.

**Revisit when:** injection queue의 큐 대기 시간이 local 대비 유의미하게 크게
관측될 경우(→ `61`을 낮춘다), 또는 `budgetExhaustedCount`가 높아 한 액터가 worker를
오래 붙드는 것이 확인될 경우(→ `32`를 낮춘다).

steal victim 선택도 **단순 순회로 시작**하며 최적화하지 않았다.

**Revisit when:** `stealAttemptCount` 대비 `stealSuccessCount`가 낮아 헛도는 스캔이
비용으로 잡힐 경우.

mutex deque 선택 역시 측정 없는 보수적 결정이다.

**Revisit when:** profiling에서 local deque 뮤텍스가 유의미한 hot path로 나타날 경우.
그때 Chase-Lev를 다시 검토한다.
