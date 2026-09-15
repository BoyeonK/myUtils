# Open Questions

아직 **결정하지 못한** 사안을 모아둔다. 여러 세션·여러 에이전트가 같은 문서를 보고
이어서 논의하기 위한 공간이다.

## 참여 규칙

읽는 주체가 이전 대화 맥락을 전혀 모른다고 가정한다. 그래서:

1. **각 항목은 그것만 읽고 판단할 수 있어야 한다.** 배경을 "위에서 말했듯이"로 생략하지 말 것.
2. **의견에는 날짜와 주체를 남긴다.** `- (2026-09-15, Claude Opus 5) ...` 형식.
   서명 없는 주장은 나중에 온 사람이 신뢰도를 판단할 수 없다.
3. **`design/adr/`에 있는 것은 이미 결정된 사안이다.** 다시 논쟁하지 말 것.
   정말 뒤집어야 한다면 여기에 **그 ADR을 명시한 새 질문**을 열어라.
4. **`Blocked on: 측정`인 항목은 의견을 더 쌓지 말 것.** 숫자가 없으면 결론이 안 난다.
   할 수 있는 일은 "무엇을 측정할지"를 더 정확하게 만드는 것뿐이다.
5. **결론이 나면 ADR을 쓰고 여기서는 지운다.** `Status`를 `Resolved → ADR-00XX`로
   바꾼 뒤 다음 정리 때 삭제한다. 이 파일은 누적 로그가 아니다.
6. **질문이 무효화되면 ADR 없이 지운다.** 대상이 재작성되거나 전제가 깨져서 물음
   자체가 사라진 경우다. 단 **무효가 된 이유는 반드시 다른 곳에 남긴다** —
   해당 코드가 재작성 대상이라는 사실은 `CLAUDE.md`에, 할 일로 남는 부분은
   `progress.md`에 옮긴다. 그래야 다음 사람이 같은 질문을 다시 열지 않는다.

## `progress.md`와의 경계

| | |
|---|---|
| `progress.md` TODO | **무엇을 할지는 정해졌고** 아직 안 한 일 |
| 이 파일 | **무엇을 할지를 아직 못 정한** 일 |

"오늘 바로 착수할 수 있는가"로 구분한다. 착수하려면 먼저 골라야 하는 게 있으면 여기다.
같은 사안을 양쪽에 쓰지 말고 한쪽에서 다른 쪽을 링크한다.

## 번호

한 번 쓴 번호는 재사용하지 않는다. 지워진 질문 자리는 비워둔다 — 과거 논의나 커밋
메시지가 그 번호를 가리키고 있을 수 있기 때문이다. 현재 비어 있는 번호:

- **Q-004** (ObjectPool TLS 소멸 순서), **Q-005** (Actor 메시지 전달 경로 할당),
  **Q-007** (`Actor::_messageCount`의 의도) — 2026-09-15 무효.
  대상 코드(`Actor`, `ObjectPool`)를 재작성하기로 하고 저장소에서 제거했다.

---

## Q-001 SendBuffer 튜닝 값 세 개

- Status: Open
- Opened: 2026-09-15
- Blocked on: **측정**
- Affects: `include/MyUtils/SendBuffer.h` 정책 상수, ADR-0001, ADR-0004

### 무엇을 정해야 하는가

```
DEFAULT_CHUNK_CAPACITY      = 16 KB   (잠정)
LARGE_ALLOCATION_THRESHOLD  = 16 KB   (잠정, CAPACITY와 동일)
DEFAULT_POOL_IDLE_LIMIT     = 64      (잠정)
```

셋 다 correctness와 무관한 튜닝 값이며, 현재 값은 **근거 없이 정한 출발점**이다.
하나의 질문으로 묶은 이유는 셋이 같은 측정 결과로 동시에 결정되기 때문이다.

### 배경

chunk는 거기서 나간 조각이 **전부** 해제돼야 반납된다(ADR-0001). 그래서 응답이 느린
클라이언트 하나가 200바이트 패킷을 붙잡으면 chunk 전체가 고정된다. chunk가 클수록
이 증폭이 커지고, 작을수록 교체가 잦아진다.

`LARGE_ALLOCATION_THRESHOLD`는 CAPACITY보다 **작게** 둘 수 있다. 낮추면 큰 요청이
current chunk를 교체하며 남은 공간을 버리는 일이 줄지만, 전용 chunk 할당이 늘어난다.

`DEFAULT_POOL_IDLE_LIMIT`은 유휴 chunk 캐싱 개수다. 낮으면 피크 후 재할당이 잦고,
높으면 피크 메모리가 그대로 남는다.

### 무엇이 이 질문을 닫는가

실제 워크로드에서 `SnapshotStats()`를 뽑은 뒤:

| 지표 | 판단 |
|---|---|
| `LiveChunkCount()` × CAPACITY 가 실제 전송 바이트 대비 몇 배인가 | 배수가 크면 CAPACITY를 낮춘다 |
| `discardedBytes` 비중 | 크면 THRESHOLD를 낮춘다 |
| `chunkCreateCount / chunkAcquireCount` (pool miss율) | 높으면 IDLE_LIMIT을 올린다 |
| `largeAllocCount` 비중 | 높으면 CAPACITY를 올리거나 THRESHOLD를 재검토 |

**대표 워크로드 없이 책상에서 정할 수 없다.** 현재 테스트 스위트의 숫자는 합성이라
근거가 되지 않는다.

---

## Q-002 ChunkRef를 intrusive refcount로 바꿀 것인가

- Status: Open
- Opened: 2026-09-15
- Blocked on: **측정**
- Affects: `ChunkRef` 별칭, ADR-0003

### 무엇을 정해야 하는가

현재 `using ChunkRef = std::shared_ptr<SendBufferChunk>`다. ADR-0003은 정확성을
우선해 shared_ptr을 택하고, 교체 판단을 profiling 이후로 미뤘다.

### 배경

control block 할당은 **조각마다가 아니라 chunk 획득마다** 발생한다. 16 KB chunk에
200 B 패킷이면 패킷당 약 0.0125회다. 조각을 복제하는 비용은 양쪽 모두 atomic 증가
1회로 같다. 즉 이득은 chunk 빈도에만 비례한다.

교체 비용은 낮게 유지되어 있다. `ChunkRef` 별칭과 스마트 포인터 구현만 바꾸면
`SendBuffer`/`SendView`/`SendBufferManager`는 손대지 않아도 된다.

### 무엇이 이 질문을 닫는가

프로파일에서 **chunk 획득/반환 경로가 유의미한 비중으로 잡히는가**. 안 잡히면
이 질문은 `Resolved: 하지 않음`으로 닫는다.

주의: intrusive로 가면 감소를 `release`로 하고 0 도달 시 `acquire` fence를 넣어야
한다. 이걸 빠뜨리면 마지막 releaser가 `Reset()`할 때 다른 스레드의 쓰기를 못 본다.
표준 구현은 이미 정확하므로, **측정 근거 없이 바꾸면 순손실**이다.

---

## Q-003 브로드캐스트·coalescing pinning에 대응할 것인가

- Status: Open
- Opened: 2026-09-15
- Blocked on: **측정**
- Affects: `SendBufferManager::Reserve` API 표면, ADR-0002

### 무엇을 정해야 하는가

fanout이 큰 패킷을 별도 경로(정확한 크기의 standalone 버퍼)로 뺄 것인가.

### 배경

`SendView`가 복사 가능해서 브로드캐스트와 scatter/gather가 자연스럽게 지원되는데
(ADR-0002), 둘 다 chunk 고정을 증폭시킨다.

- 한 패킷을 1000명에게 보내면 **가장 느린 한 명**이 완료될 때까지 chunk가 고정된다.
- 그 chunk에 함께 들어 있던 다른 패킷들의 공간도 같이 묶인다.
- 세션이 전송 대기열에 50개를 쌓으면 최대 50개 chunk를 붙잡는다.

대응한다면 200바이트 패킷은 200바이트만 고정되고 16 KB가 묶이지 않는다.

### 선택지

| 안 | 내용 | 대가 |
|---|---|---|
| A | 아무것도 안 함 | pinning 그대로 |
| B | `Reserve(size, Lifetime::Long)` 힌트 추가 | API 표면 증가, 호출자가 판단해야 함 |
| C | 별도 함수 `ReserveStandalone(size)` | 진입점 2개 (ADR-0005에서 한 번 기각한 방향) |

B/C 모두 **호출자가 fanout을 미리 안다**는 전제가 필요하다. 브로드캐스트 직전이므로
실제로는 안다.

### 무엇이 이 질문을 닫는가

`LiveChunkCount()`가 이론상 필요량 대비 몇 배인지, 그리고 세션당 pending `SendView`
수의 피크. 증폭이 2배 이내면 A로 닫는다.

---

## Q-006 `WorkerContext` 개념을 도입할 것인가

- Status: Open
- Opened: 2026-09-15
- Blocked on: **설계 판단** (파급 범위가 큼)
- Affects: `include/MyUtils/Thread.h`, `src/Thread.cpp`, 계측 구조 전반

### 무엇을 정해야 하는가

현재 `ThreadManager::Launch(std::function<void()>)`는 raw `std::thread`를 벡터에
넣을 뿐이고, worker를 나타내는 객체가 없다. 스레드별 상태는 흩어진 `thread_local`
전역이다(`MyThreadID`, `LEndTickCount`, `LRanGen`).

`WorkerContext`를 도입하면 이것들이 한 객체에 모이고, 스케줄러가 worker 목록을
직접 순회할 수 있게 된다.

**지금이 판단하기 좋은 시점이다.** 레거시를 걷어내 라이브러리가 송신 버퍼와 스레드
런처만 남은 상태라, 어느 쪽을 골라도 고칠 코드가 거의 없다. 나중에 컴포넌트가 쌓인
뒤에 뒤집으면 훨씬 비싸진다.

### 배경

계측 설계 때 "고정 worker pool이 있으면 통계를 worker 객체 수명에 묶는 게 더
단순하다"는 제안이 나왔는데, **그 구조가 이 저장소에 없어서** 채택하지 못했다.
대신 `SendBuffer` 계측은 자가 등록 `thread_local` 블록으로 만들었다(임의 스레드에서도
동작하고 `ThreadManager`에 의존하지 않는다).

### 선택지

| 안 | 내용 |
|---|---|
| A | 도입하지 않음. 지금의 자가 등록 패턴을 표준으로 삼음 |
| B | 도입. `thread_local` 전역들과 계측을 `WorkerContext`로 이관 |

B는 **라이브러리가 "모든 스레드는 ThreadManager를 거친다"를 전제하게 만든다.**
현재 `SendBuffer`는 생 `std::thread`에서도 정상 동작한다(검증 완료).
그 성질을 포기할 것인지가 핵심이다.

### 무엇이 이 질문을 닫는가

저장소 소유자의 방향 선택. 이 라이브러리를 "ThreadManager 위에서만 도는
프레임워크"로 볼 것인지, "임의 스레드에서 쓸 수 있는 부품 모음"으로 볼 것인지에
달렸다. **기술적 우열이 아니라 지향의 문제다.**
