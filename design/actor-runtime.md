# Actor Runtime 실행 규약

구현자가 참고하는 **실행 규약**이다. 파일 하나만 봐서는 알 수 없는 것 — 여러 타입에
걸친 시퀀스, 상태표, 락 규칙 — 만 여기에 둔다.

- **왜 이 구조인가**는 [ADR-0010](adr/0010-actor-ownership-and-lifetime.md),
  [ADR-0011](adr/0011-actor-scheduling-and-serialization.md),
  [ADR-0012](adr/0012-actor-stop-failure-shutdown.md)에 있다.
- 한 파일 안에서 끝나는 규칙은 그 코드 옆 주석에 둔다.

## 범위

1차 구현은 **correctness 확보까지**이고, 스케줄링 구조는 그 다음이다.

| 1차 (구현됨) | 2차 |
|---|---|
| Message / Actor / Mailbox / ControlBlock | worker-local work-stealing deque |
| 상태 머신, lost wakeup 방지 | global queue를 injection queue로 축소 |
| **단일 global runnable queue** | work stealing |
| shutdown, correctness test | batch/quantum 튜닝 |

첫 구현의 진짜 위험은 scheduling throughput이 아니라 lifetime과 상태 전이의
correctness다. 단일 큐로 semantics를 먼저 검증한 뒤 scheduler를 교체한다.

### 목표 스케줄러 구조

> **현재의 단일 global queue는 최종 구조가 아니라 baseline scheduler다.**
> 폐기하고 다시 만드는 것이 아니라, 그 위에 아래 구조로 확장한다.

```
                    Global Injection Queue
                          (MPMC)
                            │
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
           Worker 0      Worker 1      Worker 2
           Local Deque   Local Deque   Local Deque
              ↕             ↕             ↕
               ─────── Work Stealing ───────
```

자료구조 셋의 역할이 다르므로 하나로 통일하지 않는다.

| 자료구조 | 형태 | 이유 |
|---|---|---|
| Actor Mailbox | **MPSC** | producer 여럿 → 실행권을 가진 worker 하나 |
| Global Injection Queue | **MPMC** | producer 여럿 → worker 여럿이 꺼감 |
| Worker Local Runnable | **work-stealing deque** | owner는 `push_bottom`/`pop_bottom`, thief는 `steal_top` |

**어디에 넣는지의 기준은 locality다.**

- worker가 실행 중에 만들어낸 runnable → 그 worker의 local deque (locality 활용)
- 외부 thread, I/O completion thread 등 특정 worker와 연고가 없는 runnable → injection queue

owner가 LIFO로 최근 work를 처리하고 thief가 반대쪽 오래된 work를 가져가는 것은
**locality/부하분산 heuristic이지 correctness 조건이 아니다.** Actor가 특정 worker에
고정되지 않는다는 성질(§2의 "Actor는 어느 worker에서든 실행될 수 있다")은 그대로다.

목표 worker loop:

```
local deque pop  → 있으면 실행
없으면 global injection queue
없으면 다른 worker의 deque에서 steal 시도
그래도 없으면 sleep
```

정확한 우선순서는 **2차 설계에서 정할 정책**이다. 여기 적힌 순서는 전형적인 형태일 뿐
확정이 아니다.

### 2차에서 반드시 결정해야 할 것 — 큐별 알림 정책

local deque를 넣으면 **notify가 필요한 조건이 큐마다 갈린다.**

| 큐 | notify |
|---|---|
| Global injection | **필요.** producer가 외부일 수 있고 worker 전부가 자고 있을 수 있다 |
| Local deque (owner push) | owner는 깨어 있으니 자기 일엔 불필요. **그러나 남이 훔치려면 알아야 한다** |

local push마다 notify하면 local deque를 둔 이득(중앙 경합 회피)이 사라진다. 반대로
절대 notify하지 않으면 **한 worker의 deque에 일이 쌓이는 동안 나머지가 자고 있는**
상황이 생긴다. correctness 문제는 아니지만 throughput 유실이고, §5의 lost wakeup과
같은 계열의 문제가 부하 분산 층위에서 재현되는 셈이다.

흔한 해법은 `empty → non-empty` 전이에서만 notify하거나, 유휴 worker 수를 세어 0보다
클 때만 깨우는 것이다. 이 항목은 2차 착수 시 `OPEN_QUESTIONS.md`에 Q-008로 연다.

---

## 1. 소유권 그래프

```
ActorSystem
   │
   └── shared ──► ActorRuntime
                     ├── runnable queue ── shared ──► ActorControlBlock
                     ├── sleep notifier (mutex + cv)
                     └── Registry ─────── shared ──► ActorControlBlock
                                                       ▲
                                    ActorRef ── shared ─┘

                              ActorControlBlock
                                  ├── unique ──► Actor
                                  ├── mailbox (mutex + deque<MessagePtr>)
                                  ├── atomic<ActorState>
                                  ├── atomic<bool> finalizeStarted
                                  └── weak ──► ActorRuntime
```

Scheduler와 Registry는 역할이 다르지만 **하나의 내부 객체 `ActorRuntime`으로
합쳐져 있다.** ACB가 둘 다 참조해야 하는데 weak 포인터를 두 개 들 이유가 없어서다.
아래 표의 소유 방향은 그대로다.

| 참조 | 이유 |
|---|---|
| Registry → ACB (**shared**) | Actor의 논리적 수명을 유지한다. `ActorRef`를 다 놓아도 죽지 않는다 |
| ActorRef → ACB (shared) | 외부 접근 핸들일 뿐. 수명을 결정하지 않는다 |
| runnable queue → ACB (shared) | 스케줄링 중 ACB를 살려둔다 |
| ACB → Actor (**unique**) | Actor mutable state의 유일 소유자. 외부에서 직접 접근할 경로가 없다 |
| ACB → Runtime (**weak**) | 순환 방지. 시스템이 먼저 사라져도 안전하게 판정 가능 |

`Finalize`는 함수 진입부에서 `shared_from_this()`로 자기 자신을 붙잡아야 한다.
Registry unregister가 마지막 참조를 놓으면 그 자리에서 ACB가 파괴되어, 뒤따르는
`state.store(DEAD)`가 dangling이 된다.

---

## 2. 상태와 전이

```cpp
enum class ActorState : std::uint8_t { IDLE, SCHEDULED, RUNNING, STOPPING, DEAD };
```

| From | To | 주체 | 방법 | 비고 |
|---|---|---|---|---|
| IDLE | SCHEDULED | producer 또는 worker | CAS | **성공자만** runnable 등록 |
| SCHEDULED | RUNNING | worker | CAS | 실패 = STOPPING 감지 |
| RUNNING | IDLE | worker | CAS | 실패 = STOPPING 감지 |
| IDLE / SCHEDULED / RUNNING | STOPPING | `Stop()` | mailbox 락 안에서 **CAS 루프** | 아래 §5 |
| STOPPING | DEAD | Finalize 승자 | store | cleanup **완료 후** |

**금지 전이:** `DEAD → 무엇이든`, `IDLE → RUNNING` 직행, `STOPPING → IDLE/SCHEDULED/RUNNING`,
SCHEDULED 중복 진입.

`IDLE → SCHEDULED`를 CAS로 단일화하는 것이 runnable 큐 중복 등록을 **구조적으로**
막는다. 큐에 같은 ACB가 두 번 들어갈 경로 자체가 없다.

원자 연산은 1차 구현에서 전부 `seq_cst`를 쓴다(ADR-0011).

---

## 3. 락 규칙

### L1. `Actor::Handle()`은 mailbox 락을 보유한 채 호출하지 않는다

> **Mailbox 락의 책임은 큐 조작과 acceptance 직렬화에 한정한다.
> user code인 `Actor::Handle()`은 어떤 경우에도 mailbox 락을 보유한 채 호출하지 않는다.**

이 규약 전체가 여기에 의존한다. 어기면 셋이 동시에 깨진다.

| 패턴 | 결과 |
|---|---|
| handler 안에서 자기 자신에게 `Send` | mailbox 락 재진입 → 데드락 |
| handler 안에서 자기 자신을 `Stop` | acceptance gate가 같은 락 → 데드락 |
| 메시지 소멸자가 `Send` 호출 | 같은 재진입 |

**"배치 처리니까 락을 한 번만 잡자"는 최적화가 자연스럽게 들어올 자리다.** 메시지는
반드시 하나씩 pop하고 락을 놓은 뒤 `Handle`을 부른다.

### L2. registry 락과 mailbox 락을 중첩하지 않는다

Finalize가 mailbox discard와 registry unregister를 모두 하므로, 순서대로 분리해서
수행한다(§6).

### L3. sleep 락은 다른 락을 보유한 채 잡지 않는다

`Scheduler::EnqueueRunnable`이 sleep 락을 만진다. 이 함수는 항상 mailbox 락을 놓은
뒤에 호출한다.

### L4. 메시지 파괴는 mailbox 락 밖에서 한다

메시지가 `ActorRef`를 들고 있고 소멸자가 다시 `Send`할 수 있다(L1과 같은 이유).

---

## 4. 프로토콜 A — Mailbox lost wakeup 방지

**유실되는 것:** 메일박스에 메시지가 있는데 ACB가 runnable로 등록되지 않는 상태.
그 액터만 영구히 멈춘다.

```
Producer                          Worker
--------                          ------
lock(mailbox)                     CAS(SCHEDULED → RUNNING)
  state가 STOPPING/DEAD면 false
  push(message)                   budget 만큼 drain
unlock(mailbox)                     (한 건씩 pop → 락 해제 → Handle)

         ①                        CAS(RUNNING → IDLE)
CAS(IDLE → SCHEDULED)                       ②
  성공 시 EnqueueRunnable         lock(mailbox); 비었나?; unlock
                                  안 비었으면 CAS(IDLE → SCHEDULED)
                                    성공 시 EnqueueRunnable
```

**순서 제약은 둘뿐이다.** push는 ①보다 **먼저**, 재확인은 ②보다 **나중**.

| 경합 | 결과 |
|---|---|
| ①이 ②보다 먼저 | CAS 실패(state=RUNNING). 하지만 `push < ① < ② < 재확인`이므로 mailbox 뮤텍스의 happens-before로 worker가 메시지를 본다 → 재등록 |
| ①이 재확인보다 나중 | state가 IDLE → CAS 성공 → 등록 |
| 겹침 | CAS는 하나만 성공. 진 쪽은 등록하지 않는다 → 중복 없음 |

mailbox 뮤텍스가 push와 재확인 사이의 visibility를 만들어주는 것이 증명의 축이다.
lock-free 메일박스로 바꾸면 "비었다"의 의미를 여기서 다시 증명해야 한다.

---

## 5. 프로토콜 B — Worker sleep lost wakeup 방지

**유실되는 것:** runnable 큐에 ACB가 있는데 모든 worker가 자고 있는 상태.
런타임 전체가 멈춘다.

```
EnqueueRunnable(acb)              Worker loop
--------------------              -----------
runnable.enqueue(acb)             try_dequeue → 실패
{ lock(sleep); }  ← 빈 임계구역    lock(sleep)
notify_one()                        stopping이면 종료
                                    try_dequeue 재확인 → 실패
                                    cv.wait(lock)   ← 여기서 락 해제
```

**빈 임계구역이 장치다.** worker가 "재확인 → wait 진입" 구간에 있으면 `EnqueueRunnable`은
락을 얻지 못해 대기하고, worker가 `wait`에 들어가 락을 놓은 뒤에야 notify가 나간다.
worker가 아직 구간에 들어오지 않았다면 재확인 시점에 enqueue가 이미 끝나 있어 큐에서
발견한다.

- `cv.wait`은 spurious wakeup을 허용하는 **루프 안에서** 쓴다.

### 유지해야 할 invariant

> **모든 runnable 생성은 Scheduler의 scheduling entry point를 거치며,
> 어느 큐에 들어가든 필요한 wakeup notification이 함께 수행된다.**

"모든 runnable을 global queue에 넣는다"는 뜻이 **아니다.** 2차에서 `EnqueueRunnable`은
locality를 보고 **어느 큐에 넣을지 결정하는 단일 진입점**이 된다.

```
EnqueueRunnable(acb)
    ├─ 현재 worker에서 locality를 활용할 수 있다  → CurrentWorker.LocalDeque.push
    └─ 그렇지 않다                                → GlobalInjectionQueue.enqueue
    ↓
    필요하면 idle worker notify   ← 큐 종류와 무관하게 이 단계를 건너뛰지 않는다
```

위험한 것은 **큐에 직접 넣고 이 진입점을 우회하는 경로**다. 그 경로에서만 worker가
깨지 않으므로 재현이 산발적이고 원인을 찾기 어렵다. 지금은 경로가 하나뿐이라 실수할
여지가 없지만, local deque가 생기는 순간 경로가 늘어난다.

큐별로 notify가 필요한 조건이 다르다는 점은 범위 절의 "2차에서 반드시 결정해야 할 것"
참고.

---

## 6. Send / Stop acceptance protocol

### Send

```
lock(mailbox)
  state가 STOPPING 또는 DEAD  → return false
  mailbox.push_back(message)
unlock(mailbox)

TrySchedule()          ← 락 밖 (L3)
return true
```

### Stop

```
lock(mailbox)
  for (;;) {
      prev = state.load()
      if (prev == STOPPING || prev == DEAD) { unlock; return; }
      if (state.CAS(prev, STOPPING)) break;      ← prev를 원자적으로 포착
  }
unlock(mailbox)

prev == IDLE                  → 호출자가 Finalize
prev == SCHEDULED / RUNNING   → worker가 전이 CAS 실패로 감지해 Finalize
```

> **`prev = state.load(); ... state.store(STOPPING)` 형태로 쓰면 안 된다.**
> `IDLE → SCHEDULED` CAS는 mailbox 락 **밖에서** 일어나므로, load와 store 사이에
> producer가 SCHEDULED로 바꾸고 큐에 등록할 수 있다. 그러면 Stop 호출자는
> `prev == IDLE`로 믿고 Finalize하는데 ACB는 큐에 들어가 있다. once gate와 Run 진입
> CAS 덕분에 UAF로는 번지지 않지만, Finalize 주체 판정이 틀린다.
> **CAS 루프로 prev를 원자적으로 포착해야 한다.**

mailbox 락은 **acceptance를 선형화**하고, CAS 루프는 **락 밖 전이와의 경합**을 막는다.
둘 다 필요하다.

| 락 획득 순서 | Send 결과 | 메시지 운명 |
|---|---|---|
| Send → Stop | `true` | Stop이 discard |
| Stop → Send | `false` | 받지 않음 |
| Stop → Stop | — | 두 번째는 `prev == STOPPING` → 아무것도 안 함 |

**`Send() == true`는 "호출 시점에 accept됨"만 의미한다. delivery guarantee가 아니다.**

---

## 7. Finalize sequence

**once gate로 정확히 한 번만 실행된다.** 진입 후보가 넷이다 — IDLE 상태의 Stop
호출자, SCHEDULED를 집은 worker, RUNNING worker, shutdown cleanup.

```
finalizeStarted.exchange(true) == true  → 이미 누가 하고 있다. 반환

lock(mailbox)
  drained.swap(mailbox)        ← 락 안에서는 swap만
unlock(mailbox)

drained.clear()                ← 락 밖 (L4)
actor.reset()                  ← 락 밖. Actor 소멸자가 Send할 수 있다
registry.Unregister(this)      ← mailbox 락 밖 (L2)

state.store(DEAD)              ← cleanup 완료 후
```

`DEAD`를 마지막에 쓰는 것이 의미상 맞다. STOPPING과 DEAD 사이에도 `Send`는 거부되므로
(§6) 틈이 생기지 않는다.

Finalize 이후 ACB는 살아 있을 수 있다 — `ActorRef`나 runnable 큐가 참조를 들고 있으면.
그 상태로 worker가 집어도 Run 진입 CAS가 실패해 **no-op**이 된다.

---

## 8. Shutdown sequence

```
1. ActorSystem accepting = false        Spawn 거부
2. Scheduler stopping = true            sleep 락 안에서 설정 후 notify_all
3. 모든 worker join                      실행 중인 Handler 완료 대기
4. 남은 runnable 큐 discard
5. Registry 스냅샷 → 락 밖에서 전부 Finalize
6. Registry clear
7. Scheduler 참조 해제
```

**5번의 스냅샷이 필수다.** Finalize가 registry unregister를 하므로, registry를 순회하며
Finalize하면 같은 뮤텍스 재획득(데드락) 또는 iterator 무효화가 난다.

```
{ lock(registry); snapshot.assign(registry.begin(), registry.end()); }
for (acb : snapshot) acb->Finalize();          ← 락 밖
{ lock(registry); registry.clear(); }
```

3번 이후에는 worker가 더 이상 Actor를 실행하지 않으므로 Finalize가 단순해진다.

### 두 게이트의 강도가 다르다

| | 보장 |
|---|---|
| Actor 단위 `Stop()` | mailbox 뮤텍스로 **선형화**. boundary 이후 `Send`는 반드시 실패 |
| System `Shutdown()` | **best-effort**. 경합한 `Send`가 `true`를 받은 뒤 5번에서 discard될 수 있다 |

후자는 "`true` = accepted, not delivered" 계약과 일관된다. 다만 강도가 다르다는 것을
API 문서에 적어야 한다.

---

## 9. 공개 API

```cpp
struct Message { virtual ~Message() = default; };
using MessagePtr = std::unique_ptr<Message>;

class Actor {
public:
    virtual ~Actor() = default;
    virtual void Handle(Message& message) = 0;   // 런타임이 메시지를 소유한다
};

class ActorRef {
public:
    bool IsValid() const;
    bool Send(MessagePtr message) const;   // true = accepted (delivery 보장 아님)
    void Stop() const;                     // 즉시 중단. pending 메시지 discard
};

class ActorSystem {
public:
    explicit ActorSystem(std::size_t workerCount);   // workerCount >= 1 강제
    ~ActorSystem();                                   // Shutdown 포함
    ActorRef Spawn(std::unique_ptr<Actor> actor);
    void Shutdown();
};
```

`Spawn`이 `unique_ptr`을 받는 것이 중요하다. `shared_ptr`이면 호출자가 Actor state에
직접 접근할 수 있어 isolation invariant가 **API 수준에서** 뚫린다.

`workerCount == 0`은 "Send는 성공하는데 아무도 처리하지 않는" 상태를 만들므로 막는다.

메시지 budget은 한 번 실행에 **32건 고정**이다. 소진 후 메일박스가 비지 않았으면
프로토콜 A의 재확인 경로로 재등록된다.

---

## 10. Invariant ↔ Test

스펙이 코드와 어긋나는 것을 막는 장치다. 각 invariant는 자신을 검증하는 테스트를
가진다. 테스트가 깨지면 스펙이 틀렸거나 코드가 틀렸거나 둘 중 하나다.

| Invariant | Test |
|---|---|
| 동일 Actor가 동시에 두 worker에서 실행되지 않는다 | `ConcurrentExecutionForbidden` |
| 다중 producer가 동시에 보내도 corruption/유실이 없다 | `MultiProducerMailbox` |
| enqueue와 IDLE 전이 경합에서 메시지가 유실되지 않는다 | `MessageNotLostDuringIdleTransition` |
| Actor 종료 후 runnable이 실행돼도 UAF가 없다 | `DelayedRunnableAfterStop` |
| 동일 Actor가 서로 다른 worker에서 실행돼도 정상 동작한다 | `ActorMigratesBetweenWorkers` |
| Stop boundary 이후 `Send`는 성공하지 않는다 | `SendAfterStopRejected` |
| Stop과 Send가 경합해도 UAF가 없다 | `StopSendRace` |
| Shutdown 후 stale `ActorRef`의 `Send`는 실패한다 | `SendAfterShutdownRejected` |
| 남은 메시지/runnable이 있어도 shutdown이 안전하다 | `ShutdownWithPendingWork` |
| **`Handle` 중 mailbox 락을 보유하지 않는다** (L1) | `SelfSendFromHandler` |
| | `SelfStopFromHandler` |
| | `SpawnFromHandler` |
| Handler 예외 후 추가 메시지를 처리하지 않는다 | `HandlerExceptionStopsActor` |

**L1은 락 보유를 직접 관측하지 않는다.** 대신 어겼을 때 반드시 데드락하는 세 가지
사용 패턴을 테스트한다. 관측 가능한 결과로 invariant를 잡는 방식이다.

work stealing 테스트는 1차 범위 밖이다.

---

## 11. 알려진 제약

- **Handler는 무한정 block하지 않아야 한다.** block하면 shutdown의 worker join도
  끝나지 않는다. 설계가 감수하는 제약이다.
- **`Stop()`은 graceful stop이 아니다.** accept된 메시지도 버린다. "마지막 저장
  메시지를 보내고 Stop"은 조용히 실패한다. graceful이 필요해지면 `StopAfterDrain()`을
  별도 API로 추가한다 — `Stop`에 drain 의미를 섞지 않는다.
- **메시지마다 동적 할당이 발생한다.** `unique_ptr<Message>`의 대가다. correctness
  우선 1차 구현으로 수용하며, hot path로 확인되면 value/variant, pooled, inline
  type-erased 중에서 교체한다(ADR-0011).
