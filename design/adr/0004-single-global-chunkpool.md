# ADR-0004 단일 global ChunkPool과 그 수명

- Status: Accepted
- Date: 2026-09-15

## Context

chunk를 매번 `new`/`delete`하면 16 KB 블록이라 할당자가 페이지 단위 경로를 타게
된다. 재사용 풀이 필요하다.

문제가 둘이다.

1. **Contention**: 여러 worker가 하나의 전역 풀에 접근하면 병목이 되지 않을까?
2. **수명**: 프로세스 종료 시 풀이 먼저 파괴되면, 나중에 해제되는 chunk가
   이미 사라진 풀에 반환을 시도한다.

## Decision

### 구조: 단일 global MPMC 큐. thread-local 캐시를 두지 않는다

`moodycamel::ConcurrentQueue<SendBufferChunk*>` 하나로 충분하다.

빈도를 계산하면 명확하다. worker 하나가 초당 100 MB를 내보내도 16 KB chunk 기준
획득/반환은 **초당 약 6,400회**다. 뮤텍스로 감싼 `vector`여도 견딜 수치이고
contention을 걱정할 자리가 아니다.

그런데 이 시스템은 구조적으로 **A 스레드가 획득하고 B 스레드가 반환**한다
(할당은 worker, 해제는 I/O completion). thread-local 캐시를 얹으면 completion을
많이 처리하는 스레드에 chunk가 쌓이고 할당하는 스레드는 계속 비어서 결국 전역으로
내려간다. **캐시가 일하지 않으면서 메모리만 붙잡는 상태**가 된다. 단일 전역 큐는
이 드리프트를 자연히 흡수한다.

직접 lock-free 스택을 짜는 것도 배제한다. ABA를 만나기 쉽고, 위 빈도에서는 얻을 게 없다.

### 수명: pool을 `shared_ptr`로 소유하고, chunk deleter가 지분을 캡처한다

```cpp
PoolRef self = shared_from_this();
return ChunkRef(raw, [self](SendBufferChunk* chunk) noexcept {
    self->Return(chunk);
});
```

이러면 **pool이 자기가 내준 chunk 전부보다 반드시 오래 산다.** 유휴 chunk는 raw
포인터로만 보관하므로 pool → chunk → pool 순환은 생기지 않는다.

## 기각된 대안

### 의도적 leak (`static ChunkPool* p = new ChunkPool;`)

가장 간단하고 흔한 해법이며, 종료 순서 문제를 확실히 없앤다. 다만 비용이 chunk
빈도의 atomic 한 쌍과 deleter 16바이트뿐인데 깨끗한 소유 모델을 포기할 이유가 없다.
sanitizer 보고서도 깨끗해진다.

### 명시적 shutdown 순서로 해결

```
새 I/O 중단 → completion drain → worker join → SendView 전부 소멸 → Pool 파괴
```

**이건 대안이 될 수 없다.** 살아 있는 `SendView`를 열거하거나 강제로 소멸시킬 방법이
없으므로, 이 순서는 "drain 이후엔 아무도 SendView를 들고 있지 않다"는 **시스템 전역
불변식**에 의존한다. 달성은 가능하지만 send buffer 라이브러리가 국소적으로 검증할 수
없는 조건이고, 임베딩하는 쪽이 규율을 어기면 라이브러리가 조용히 깨진다.

`shared_ptr` 소유는 그 의존을 없앤다. 운영 규율로 두는 건 좋지만 **정확성을 거기
기대지 않는다.**

## Consequences

- 반환은 언제 어느 스레드에서 일어나도 안전하다. shutdown 순서와 무관하다.
- Pool 큐의 enqueue/dequeue가 소유권 이전 시점의 happens-before를 제공한다. 그래서
  **worker 사이에서 chunk를 Pool을 건너뛰고 직접 넘기면 안 된다.** 그 순간
  [ADR-0001](0001-refcounted-arena.md)의 "`_offset`은 동기화하지 않는다"가 깨진다.
- 유휴 개수 상한(`DEFAULT_POOL_IDLE_LIMIT`)을 넘겨 반환된 chunk는 실제로 해제한다.
  이게 없으면 순간 피크가 그대로 영구 점유가 된다.
- 측정 결과 정말 contention이 잡히면 bounded local cache나 origin-worker 반환 큐를
  그때 검토한다. 워크로드에 따라 이득일 수 있으므로 일반적으로 해롭다고 보지는 않는다.
