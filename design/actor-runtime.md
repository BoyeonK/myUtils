# Actor Runtime 실행 규약

구현자가 참고하는 **실행 규약**이다. 파일 하나만 봐서는 알 수 없는 것 — 여러 타입에
걸친 시퀀스, 상태표, 락 규칙 — 만 여기에 둔다.

한 파일 안에서 끝나는 규칙은 그 코드 옆 주석에 둔다.

## 0. 런타임 몸체

```
                 RuntimeBody                  ← 프로세스 전역, 함수 지역 static
                      │
        ┌─────────────┴─────────────┐
        ▼                           ▼
   WorkerPool                  ActorRegistry
 (스레드 · 큐 · 수면)          (Actor 논리 수명)
```

**worker thread pool과 그 위에서 도는 실행 주체가 하나의 몸체다.** 지금 그 위에 있는
것은 Actor뿐이지만, I/O completion이나 임의 함수자를 얹을 자리가 `WorkerPool`이다.
둘을 가른 기준이 그 확장 축이다 — 무엇이 추가되어도 `ActorRegistry`는 그대로 남는다.

| | |
|---|---|
| 인스턴스 | **프로세스에 하나.** 사용자가 만드는 런타임 객체는 없다 |
| 기동 | 첫 `Spawn`에서 lazy. 기동 경합은 magic static이 처리한다 |
| 종료 | **1회.** 재기동도, 부분 종료(pool만 / Actor만)도 없다 |
| worker 수 | 기본값은 `hardware_concurrency`(0이면 1). `Runtime::SetWorkerCount`로 **기동 전에만** 바꾼다 |

`SetWorkerCount`의 값과 기동 플래그는 **몸체 바깥**의 상수 초기화 전역에 있다. 몸체 안에
두면 그 호출 자체가 기동을 유발해 API가 자기 목적을 파괴한다. 같은 이유로
`Runtime::WorkerCount()`와 `Runtime::SnapshotStats()`도 몸체를 만지지 않는다.

### WorkerPool은 Actor를 모른다

큐에 담기는 것은 `Task`(가상 `Execute()` 하나)이고 `ActorControlBlock`이 그 구현 중
하나다. worker loop는 `task->Execute()`만 부른다.

따라서 **message budget 같은 Actor 정책은 pool이 아니라 ACB가 쥔다.** 이 경계를 허물면
다른 종류의 작업을 얹을 때 pool 내부를 다시 설계해야 한다.

### 정적 생성 순서 — 지우면 종료 때만 깨진다

> **`RuntimeBody`의 생성자는 worker를 만들기 전에 계측 전역을 먼저 건드린다.**

worker는 계측 블록을 통해 `StatsLock()`/`StatsRegistry()`/`RetiredStats()`를 만지는데,
몸체가 프로세스 전역 static이므로 이들이 몸체보다 나중에 만들어지면 **먼저 파괴되어
아직 도는 worker가 죽은 전역을 만진다.**

표준은 static storage duration 객체에 대해 "생성 완료의 역순으로 파괴"를 보장하며
([basic.start.term]) 함수나 TU 경계를 가리지 않는다. 그래서 생성자에서 먼저 건드려
순서를 `stats → body`로 고정하면 파괴는 `body → stats`가 되고, join이 끝난 뒤에야
계측 전역이 사라진다.

**"worker의 `thread_local` 소멸자가 join 중에 도니 안전하다"는 이 자리를 덮지 못한다.**
join을 시작하는 것이 몸체의 소멸자이므로, 순서가 반대였다면 worker는 join이 시작되기도
전에 죽은 전역을 만진다.

이 보장은 **몸체가 함수 지역 static 하나**라는 데 선다. 몸체를 `new`로 만들거나 소유자를
바꾸면 이 절을 다시 세워야 한다.

**알려진 위험.** 소비자가 MyUtils를 DLL에 넣고 언로드하면 `DLL_PROCESS_DETACH`에서
loader lock을 쥔 채 join하게 되어 교착한다. 현재 사용 형태(정적 라이브러리 + exe 직접
링크)에서는 발생하지 않는다. 피하려면 `Runtime::Shutdown()`을 명시적으로 부른다.

## 스케줄러 구조

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
| Worker Local Task | **work-stealing deque** | owner는 `push_bottom`/`pop_bottom`, thief는 `steal_top` |

local deque는 worker당 `std::mutex` + `std::deque`다(`Runtime.cpp`의 해당 주석 참조).

**어디에 넣는지의 기준은 locality다.**

- worker가 실행 중에 만들어낸 task → 그 worker의 local deque
- 외부 thread, I/O completion thread 등 특정 worker와 연고가 없는 task → injection queue

판정은 worker loop가 진입할 때 스스로 세팅하는 thread_local `{isWorker, index}`로 한다.

> **과거에는 여기에 런타임 포인터도 담았다.** `ActorSystem`이 둘 이상일 때 시스템 A의
> worker가 만든 B의 runnable이 A의 deque로 새는 것을 막기 위해서였고,
> `CrossRuntimeIsolation` 테스트가 그것을 잡았다. 런타임이 프로세스 전역 단일이 되면서
> 비교 대상이 사라져 둘 다 걷어냈다. **전역 단일이라는 전제가 바뀌면 여기부터
> 되돌려야 한다.**

owner가 LIFO로 최근 work를 처리하고 thief가 반대쪽 오래된 work를 가져가는 것은
**locality/부하분산 heuristic이지 correctness 조건이 아니다.** Actor가 특정 worker에
고정되지 않는다는 성질(§2의 "Actor는 어느 worker에서든 실행될 수 있다")은 그대로다.

### worker loop

```
매 61번째 반복  → injection 먼저, 없으면 local
그 외           → local 먼저, 없으면 injection
둘 다 없으면     → steal 시도
그래도 없으면    → sleep (§5)
```

**61회 주기가 없으면 injection이 굶는다.** local deque를 채우는 것은 handler가 만든
task이고, 그게 꾸준한 워크로드에서는 외부 스레드·I/O completion의 task가 무한정
밀린다. `61`은 최적값이 아니라 baseline이다.

한 번의 local 처리가 최대 32건(budget)이므로 **injection 확인 간격은 최악의 경우
메시지 1,952건이다.** 둘 중 하나를 바꾸면 다른 쪽의 의미도 바뀐다.

### 큐별 알림 정책

**두 큐는 notify가 필요한 이유가 다르다.** 같은 정책을 쓰지 않는다.

| 큐 | notify | 빠뜨리면 |
|---|---|---|
| Global injection | **항상** | 아무도 깨지 않는다. **시스템이 멈춘다** |
| Local deque | **유휴 worker가 있을 때만** | owner가 결국 처리한다. 부하 분산만 잃는다 |

local push의 notify는 **correctness 조건이 아니라 heuristic이다.** push한 주체가 그
deque의 owner이고, owner는 잠들기 전에 자기 deque를 확인하므로 진행은 보장된다.
그래서 여기에만 조건을 붙인다.

"유휴 worker가 있는가"는 `_idleCount`로 판정하며, **이 카운터는 sleep 락 안에서
증감한다.** 락 밖에서 세면 producer가 0을 읽고 notify를 건너뛴 직후 worker가 잠드는
창이 생긴다.

---

## 1. 소유권 그래프

```
RuntimeBody (프로세스 전역 static)
   ├── WorkerPool
   │      ├── injection queue  ── shared ──► Task
   │      ├── local deque × N  ── shared ──► Task
   │      └── sleep notifier (mutex + cv)
   └── ActorRegistry ─────────── shared ──► ActorControlBlock
                                              ▲
                           ActorRef ── shared ─┘

                     ActorControlBlock : Task
                         ├── unique ──► Actor
                         ├── mailbox (mutex + deque<MessagePtr>)
                         ├── atomic<ActorState>
                         └── atomic<bool> finalizeStarted
```

| 참조 | 이유 |
|---|---|
| Registry → ACB (**shared**) | Actor의 논리적 수명을 유지한다. `ActorRef`를 다 놓아도 죽지 않는다 |
| ActorRef → ACB (shared) | 외부 접근 핸들일 뿐. 수명을 결정하지 않는다 |
| pool 큐 → Task (shared) | 스케줄링 중 대상을 살려둔다 |
| ACB → Actor (**unique**) | Actor mutable state의 유일 소유자. 외부에서 직접 접근할 경로가 없다 |

**ACB는 런타임을 가리키지 않는다.** 전역 단일이라 인스턴스를 지목할 이유가 없고,
필요할 때 내부 접근자로 닿는다.

`Finalize`는 함수 진입부에서 `shared_from_this()`로 자기 자신을 붙잡아야 한다.
Registry unregister가 마지막 참조를 놓으면 그 자리에서 ACB가 파괴되어, 뒤따르는
`state.store(DEAD)`가 dangling이 된다.

### 수명 invariant — 몸체가 ACB보다 먼저 죽을 수 있다

`RuntimeBody`는 프로세스 전역 static이므로 **소비자가 더 늦게 파괴되는 `ActorRef`를
들고 있을 수 있다.** 과거에는 `ACB → Runtime`이 `weak_ptr`이라 그 경우가 자동으로
닫혔다. 지금은 아래 둘이 그 자리를 대신한다.

> **LT1. `Send`의 `STOPPING`/`DEAD` 검사는 몸체 접근보다 반드시 앞선다.**
> ACB가 몸체에 닿는 경로는 `TrySchedule`(accept된 뒤에만 실행)과 `Finalize`의
> unregister(once gate 통과 후 1회)뿐이다. 검사가 뒤로 밀리면 stale `ActorRef`의
> `Send`가 파괴된 몸체를 만진다.

> **LT2. 종료는 Registry의 모든 ACB를 `DEAD`로 만든 뒤 반환한다.**
> LT1이 의미를 가지려면 "종료 후 살아남은 ACB는 전부 `DEAD`"가 참이어야 한다.
> §8의 5번이 그것을 만든다. 이 단계를 약화하면 LT1이 무력해진다.

`ActorControlBlock`의 소멸자 자체는 몸체를 만지지 않는다 — 몸체보다 늦게 파괴되는
`ActorRef`가 마지막 참조를 놓는 것이 정상 경로다.

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

**상태 전이의 원자 연산은 전부 `seq_cst`다.** 계측 카운터나 runnable 등록 시각처럼
correctness에 관여하지 않는 값은 `relaxed`를 쓴다.

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

`WorkerPool::Submit`이 sleep 락을 만진다. 이 함수는 항상 mailbox 락을 놓은
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
  성공 시 WorkerPool::Submit         lock(mailbox); 비었나?; unlock
                                  안 비었으면 CAS(IDLE → SCHEDULED)
                                    성공 시 WorkerPool::Submit
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

**유실되는 것:** runnable이 어딘가에 있는데 모든 worker가 자고 있는 상태.
런타임 전체가 멈춘다.

```
WorkerPool::Submit(acb)              Worker loop
--------------------              -----------
어느 큐에 넣을지 고르고 push       모든 소스 비었음 확인 → 실패
{ lock(sleep); }  ← 빈 임계구역    lock(sleep)
notify_one()                        stopping이면 종료
                                    idleCount += 1
                                    모든 소스 재확인 → 실패
                                    cv.wait(lock)   ← 여기서 락 해제
                                    idleCount -= 1
```

**빈 임계구역이 장치다.** worker가 "재확인 → wait 진입" 구간에 있으면 `WorkerPool::Submit`은
락을 얻지 못해 대기하고, worker가 `wait`에 들어가 락을 놓은 뒤에야 notify가 나간다.
worker가 아직 구간에 들어오지 않았다면 재확인 시점에 push가 이미 끝나 있어 발견한다.

- `cv.wait`은 spurious wakeup을 허용하는 **루프 안에서** 쓴다.

### 재확인의 범위

> **sleep 직전 재확인은 자기 local deque, injection queue, 그리고 다른 worker의
> deque까지 모두 본다.** 전부 sleep 락을 **보유한 채** 수행한다.

큐가 셋으로 늘어도 **프로토콜의 구조는 바뀌지 않았다.** 넓어진 것은 "큐가 비었나"의
정의뿐이다.

다른 worker의 deque까지 보는 이유는 위 "큐별 알림 정책"의 heuristic 때문이다.
보지 않으면 이런 순서가 성립한다 — A가 자기 deque에 push(유휴 0이라 notify 생략)
→ B가 잠들며 재확인 → 자기 것과 injection만 비어 있음을 보고 수면. A가 긴 handler에
묶이면 그 일은 A가 풀려날 때까지 멈춘다.

비용은 잠들기 직전에만 드는 O(worker 수) 스캔이다. hot path가 아니지만 worker 수가
커지면 재검토 대상이다.

### 유지해야 할 invariant

> **모든 task 등록은 `WorkerPool::Submit`을 거치며,
> 어느 큐에 들어가든 필요한 wakeup notification이 함께 수행된다.**

"모든 task를 global queue에 넣는다"는 뜻이 **아니다.** `Submit`은 locality를 보고
**어느 큐에 넣을지 결정하는 단일 진입점**이다.

```
WorkerPool::Submit(task)
    ├─ tls.isWorker  → 그 worker의 LocalDeque.push_bottom
    │                    유휴 worker가 있을 때만 notify
    └─ 그렇지 않다    → GlobalInjectionQueue.enqueue
                         항상 notify
```

위험한 것은 **큐에 직접 넣고 이 진입점을 우회하는 경로**다. 그 경로에서만 worker가
깨지 않으므로 재현이 산발적이고 원인을 찾기 어렵다. 큐가 셋이 된 지금 더 중요해졌다.

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

### Consumption boundary — drain 쪽의 짝

`Send`에 acceptance boundary가 있듯 drain 루프에도 대칭되는 경계가 필요하다.

> **worker는 메시지를 pop하기 전, mailbox 락 안에서 state가 여전히 `RUNNING`인지
> 확인한다. 아니면 drain 루프를 즉시 탈출한다.**

```
for (budget 만큼) {
    lock(mailbox)
      state != RUNNING?  → break        ← consumption boundary
      mailbox 비었나?     → break
      message = pop_front()
    unlock(mailbox)

    Handle(message)                     ← 락 밖 (L1)
}
```

**`Stop`은 state만 바꿀 뿐 worker는 여전히 drain 루프 안에 있다.** 이 검사가 없으면
`Stop` 이후에도 pending 메시지를 budget 한도(31건)까지 계속 처리한다 — `Stop`이
pending을 버린다는 계약(§11)과 정면으로 어긋난다. handler 예외 경로에만 `break`가
있고 이쪽에 없으면 같은 상황을 두 가지로 처리하는 것이 된다.

**검사를 `Handle()` 반환 직후가 아니라 락 안에 두는 이유**는 `Stop`이 바로 그 락을
쥔 채 `STOPPING`을 쓰기 때문이다. 같은 락 안에서 읽으면 경계가 이렇게 닫힌다.

> Stop이 mailbox 락을 통과한 시점 이후로는 어떤 메시지도 새로 pop되지 않는다.

`Handle()` 반환 직후에 검사하면 검사와 다음 pop 사이에 창이 남아 이 문장이 성립하지
않는다. 어차피 pop을 위해 잡는 락이므로 추가 비용도 없다.

적용 대상은 self-stop만이 아니다. **`Handle()` 실행 중에 들어온 모든 `Stop`** — 다른
스레드의 `Stop`, `Shutdown` 경로, handler 자신의 `Stop` — 이 전부 여기서 관측된다.

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

Finalize 이후 ACB는 살아 있을 수 있다 — `ActorRef`나 pool 큐가 참조를 들고 있으면.
그 상태로 worker가 집어도 Execute 진입 CAS가 실패해 **no-op**이 된다.

---

## 8. Shutdown sequence

`RuntimeBody::Shutdown`이 순서를 잡는다. **멱등이며, 부분 종료는 없다.**

```
1. ActorRegistry accepting = false      Registry 락 안에서 설정, Spawn 등록과 선형화
2. WorkerPool stopping = true           sleep 락 안에서 설정 후 notify_all
3. 모든 worker join                      실행 중인 Execute 완료 대기
4. 남은 Task discard                     injection과 worker local deque 전부
5. Registry 스냅샷 → 락 밖에서 전부 Stop 후 Finalize
6. Registry clear
```

**3번은 block하고 중단 수단이 없다.** 긴 handler는 곧 긴 종료다. 종료 지연을 제한해야
한다면 handler 쪽에 시한을 두는 것이 유일한 수단이다(§11).

**4번의 의미는 "pool은 종료 시 남은 Task를 실행하지 않고 버린다"이다.** Actor Task는
버려도 ACB가 Registry 참조로 살아 있어 5번이 확정적으로 정리한다. 다른 종류의 Task가
생기면 이 계약을 먼저 본다.

**5번의 스냅샷이 필수다.** Finalize가 registry unregister를 하므로, registry를 순회하며
Finalize하면 같은 뮤텍스 재획득(데드락) 또는 iterator 무효화가 난다.

**1번과 `Spawn`의 Registry 등록은 같은 Registry 락을 쓴다.** `Spawn`의 사전
`accepting` 확인은 불필요한 할당을 피하는 fast path일 뿐이고, 실제 등록 허용 여부는
락 안에서 다시 확인한다. 따라서 락을 먼저 통과해 등록된 Actor는 5번 snapshot에 반드시
포함되고, 1번 이후에 등록을 시도한 `Spawn`은 무효 `ActorRef`를 반환한다.

```
{ lock(registry); snapshot.assign(registry.begin(), registry.end()); }
for (acb : snapshot) { acb->Stop(); acb->Finalize(); }   ← 락 밖
{ lock(registry); registry.clear(); }
```

**`Stop()`을 먼저 부르는 것이 `Finalize()`만 부르는 것보다 강하다.** `Finalize`는 state를
바로 `DEAD`로 쓰지만 그 전까지는 `Send`가 accept될 수 있고, `Stop`은 mailbox 락 안에서
`STOPPING`으로 전이해 그 창을 닫는다. `Finalize`는 once gate가 있어 `Stop`이 이미
수행했더라도 안전하다.

3번 이후에는 worker가 더 이상 Actor를 실행하지 않으므로 Finalize가 단순해진다.

### 두 게이트의 강도가 다르다

| | 보장 |
|---|---|
| Actor 단위 `Stop()` | mailbox 뮤텍스로 **선형화**. boundary 이후 `Send`는 반드시 실패 |
| `Runtime::Shutdown()` | **best-effort**. 경합한 `Send`가 `true`를 받은 뒤 5번에서 discard될 수 있다 |

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
    bool IsValid() const noexcept;
    explicit operator bool() const noexcept;
    bool Send(MessagePtr message) const;   // true = accepted (delivery 보장 아님)
    void Stop() const;                     // 즉시 중단. pending 메시지 discard
};

// MyUtils/Actor.h — namespace MyUtils::Actors
[[nodiscard]] ActorRef Spawn(std::unique_ptr<Actor> actor);   // 첫 호출이 런타임을 기동
std::size_t LiveActorCount();                                 // 계측·테스트용. 전역 누적값
inline constexpr std::size_t DEFAULT_MESSAGE_BUDGET = 32;

// MyUtils/Runtime.h — namespace MyUtils::Runtime
void SetWorkerCount(std::size_t count);   // 기동 전에만. 위반은 MYUTILS_ASSERT
std::size_t WorkerCount() noexcept;       // 기동시키지 않는다
void Shutdown();                          // 몸체 전체. 멱등. block한다
struct Stats { /* ... */ };
Stats SnapshotStats();                    // 기동시키지 않는다
inline constexpr std::size_t INJECTION_POLL_INTERVAL = 61;
```

**사용자가 만드는 런타임 객체는 없다.** `ActorSystem`은 2026-09-21에 제거되었다.

`Spawn`이 `unique_ptr`을 받는 것이 중요하다. `shared_ptr`이면 호출자가 Actor state에
직접 접근할 수 있어 isolation invariant가 **API 수준에서** 뚫린다.

`SetWorkerCount(0)`은 "Send는 성공하는데 아무도 처리하지 않는" 상태를 만들므로 막는다.
기동 후 호출도 막는다 — **설정이 조용히 무시되는 것이 최악**이기 때문이다. 기동 전
반복 호출은 마지막 값이 이긴다.

`LiveActorCount()`는 런타임이 전역이라 **전역 누적값**이다. 특정 Actor가 정리되었는지
보려면 기준선을 잡고 그 차이를 본다.

메시지 budget은 한 번 실행에 **32건 고정**이다. 소진 후 메일박스가 비지 않았으면
프로토콜 A의 재확인 경로로 재등록된다. **이 상수는 Actor의 정책이므로 ACB가 쥔다** —
`WorkerPool`은 알지 못한다.

---

## 10. Invariant ↔ Test

스펙이 코드와 어긋나는 것을 막는 장치다. 각 invariant는 자신을 검증하는 테스트를
가진다. 테스트가 깨지면 스펙이 틀렸거나 코드가 틀렸거나 둘 중 하나다.

**실행 파일이 넷인 것은 취향이 아니라 제약이다.** 런타임이 전역 단일이고 1회 기동·1회
종료라, 한 프로세스는 **worker 수 하나와 종료 한 번**만 가질 수 있다. 필요한 런타임
형상이 다르면 프로세스를 나눠야 한다.

| Invariant | Test | 실행 파일 |
|---|---|---|
| 동일 Actor가 동시에 두 worker에서 실행되지 않는다 | `ConcurrentExecutionForbidden` | `ActorTest` (4) |
| 다중 producer가 동시에 보내도 corruption/유실이 없다 | `MultiProducerMailbox` | `ActorTest` |
| enqueue와 IDLE 전이 경합에서 메시지가 유실되지 않는다 | `MessageNotLostDuringIdleTransition` | `ActorTest` |
| Actor 종료 후 task가 실행돼도 UAF가 없다 | `DelayedRunnableAfterStop` | `ActorTest` |
| 동일 Actor가 서로 다른 worker에서 실행돼도 정상 동작한다 | `ActorMigratesBetweenWorkers` | `ActorTest` |
| Stop boundary 이후 `Send`는 성공하지 않는다 | `SendAfterStopRejected` | `ActorTest` |
| Stop과 Send가 경합해도 UAF가 없다 | `StopSendRace` | `ActorTest` |
| **`Handle` 중 mailbox 락을 보유하지 않는다** (L1) | `SelfSendFromHandler` | `ActorTest` |
| | `SelfStopFromHandler` | `ActorTest` |
| | `SpawnFromHandler` | `ActorTest` |
| Handler 예외 후 추가 메시지를 처리하지 않는다 | `HandlerExceptionStopsActor` | `ActorTest` |
| **Stop 이후 pending 메시지를 처리하지 않는다** (consumption boundary, §6) | `SelfStopFromHandler` | `ActorTest` |
| local deque에 쌓인 일을 다른 worker가 나눠 가진다 | `WorkStealingBalancesLoad` | `ActorTest` |
| worker가 아닌 스레드의 `Send`도 처리된다 | `InjectionFromExternalThread` | `ActorTest` |
| local 작업이 꾸준해도 injection이 굶지 않는다 | `InjectionNotStarvedByLocalWork` | `ActorSingleWorkerTest` (1) |
| 종료·Spawn gate·stale `Send`·멱등 종료 | `ShutdownScenario` | `ActorShutdownTest` (2) |
| worker 수를 설정하지 않아도 동작한다 | `RunsWithoutConfiguration` | `RuntimeConfigTest` (기본값) |
| `WorkerCount()`가 런타임을 기동시키지 않는다 | `WorkerCountConfiguration` | `ActorTest` |
| ~~다른 런타임의 local deque로 task가 새지 않는다~~ | ~~`CrossRuntimeIsolation`~~ | **폐기** |

**L1은 락 보유를 직접 관측하지 않는다.** 대신 어겼을 때 반드시 데드락하는 세 가지
사용 패턴을 테스트한다. 관측 가능한 결과로 invariant를 잡는 방식이다.

같은 이유로 **LT1(§1)도 직접 관측하지 않는다.** 몸체가 프로세스 전역이라 프로세스 안에서
파괴시킬 수 없다. 종료 후 stale `Send`/`Stop`이 크래시 없이 `false`를 돌려주는 것으로
간접 확인한다.

**`CrossRuntimeIsolation`은 2026-09-21에 폐기되었다.** 런타임이 전역 단일이 되어 복수
런타임이라는 상황 자체가 성립하지 않는다.

**`InjectionNotStarvedByLocalWork`가 자기 실행 파일을 갖는 이유**는 worker가 하나여야
결정적이기 때문이다. worker가 여럿이면 다른 worker가 훔쳐가 상황이 만들어지지 않고,
나머지를 handler로 붙잡아 재현하려 해도 어느 worker에 배정될지 보장되지 않는다.

**`ShutdownScenario`가 종료 관련 invariant를 한 덩어리로 담는 이유**는 종료가 프로세스당
한 번뿐이기 때문이다. 하나의 종료에 pending work·경합 Spawn·stale `Send`·멱등 재호출을
모두 얹는다.


---

## 11. 알려진 제약

- **Actor 간 공정성을 보장하지 않는다.** 스케줄된 Actor의 eventual execution은
  보장 대상이 아니다. worker가 하나뿐이고 어떤 Actor가 handler마다 자기 자신에게 메시지를
  보내면, 그 Actor가 local deque 뒤쪽에 계속 재등록되어 앞쪽의 다른 Actor가 실행되지 않을
  수 있다(훔쳐갈 worker가 없다). **이 상태는 스케줄러가 고칠 문제가 아니라 생산이 소비를
  넘어섰다는 신호로 본다** — worker 수나 생산 쪽에서 대응한다.
- **Handler는 무한정 block하지 않아야 한다.** block하면 `Runtime::Shutdown`의 worker
  join도 끝나지 않는다. **중단 수단은 없고 만들지 않는다.** 설계가 감수하는 제약이며,
  종료 지연을 제한해야 한다면 handler 쪽에 시한을 두는 것이 유일한 수단이다.
- **종료 이후에는 pool도 쓸 수 없다.** 런타임 전체가 하나의 몸체라 재기동도 부분 종료도
  없다. 추후 I/O·임의 함수자를 얹을 때, "Actor를 다 정리한 뒤에도 I/O는 돌아야 하는가"를
  먼저 결정해야 한다.
- **`Stop()`은 graceful stop이 아니다.** accept된 메시지도 버린다. "마지막 저장
  메시지를 보내고 Stop"은 조용히 실패한다. graceful이 필요해지면 `StopAfterDrain()`을
  별도 API로 추가한다 — `Stop`에 drain 의미를 섞지 않는다.
- **메시지마다 동적 할당이 발생한다.** `unique_ptr<Message>`의 대가다. correctness
  우선 1차 구현으로 수용하며, hot path로 확인되면 value/variant, pooled, inline
  type-erased 중에서 교체한다.
