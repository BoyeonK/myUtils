# Architecture Decision Records

내린 결정과 **검토했다 버린 대안**을 남기는 곳이다.

## 이 문서들의 성격

ADR은 **그 시점의 결정을 박제한 기록**이지 현재 상태를 설명하는 문서가 아니다.
결정이 바뀌면 기존 문서를 고치지 말고, 새 ADR을 쓴 뒤 기존 문서의 `Status`를
`Superseded by ADR-00XX`로 바꾼다. 그래서 낡은 ADR은 틀린 게 아니라 역사다.

## 무엇을 어디에 쓰는가

| 내용 | 위치 |
|---|---|
| 이 줄을 바꾸면 무엇이 깨지는가 | **코드 주석** |
| 지금 여기서 작업하려면 알아야 할 것 (현재 상태, 규칙, 함정) | **CLAUDE.md** |
| 왜 이 모양으로 정했고 무엇을 버렸는가 | **여기** |
| **아직 정하지 못한 것** | [`../OPEN_QUESTIONS.md`](../OPEN_QUESTIONS.md) |

같은 내용을 여러 곳에 중복해서 쓰지 않는다.

여기 있는 것은 **이미 결정된 사안**이다. 뒤집으려면 `OPEN_QUESTIONS.md`에 해당
ADR을 명시한 새 질문을 열고, 결론이 나면 새 ADR을 쓴 뒤 기존 문서의 `Status`를
`Superseded by ADR-00XX`로 바꾼다.

### 기존 문서를 어디까지 건드릴 수 있나

- **Context / Decision / 기각된 대안은 고치지 않는다.** 그때의 판단을 그대로 둔다.
- **`Status`와 날짜 찍힌 "후속" 절은 추가할 수 있다.** 그게 ADR이 낡지 않는 방식이다.
  결정이 바뀐 게 아니라 **유보했던 것이 닫혔을 때**는 새 ADR을 만들지 말고 원래
  문서에 후속을 붙인다. 같은 결정이 두 곳에 생기는 것을 막기 위해서다.
  (예: ADR-0003의 "언제 intrusive로 바꿀 것인가")

## 목록

### 라이브러리 전반

| # | 제목 | Status |
|---|---|---|
| [0009](0009-parts-not-framework.md) | WorkerContext를 도입하지 않는다 | Accepted |

### 송신 버퍼 (`SendBuffer`)

| # | 제목 | Status |
|---|---|---|
| [0001](0001-refcounted-arena.md) | SendBuffer를 refcounted arena + bump allocation으로 | Accepted |
| [0002](0002-sendbuffer-sendview-split.md) | SendBuffer / SendView 타입 분리 | Accepted |
| [0003](0003-chunk-lifetime-shared-ptr.md) | chunk 수명을 shared_ptr로 관리 | Accepted |
| [0004](0004-single-global-chunkpool.md) | 단일 global ChunkPool과 그 수명 | Accepted |
| [0005](0005-reserve-commit-tail-reclaim.md) | Reserve 단일 진입점과 tail reclaim 조건 | Accepted |
| [0006](0006-size-validation-and-alignment.md) | 크기 검증 계층, 실패 정책, alignment 제외 | Accepted |
| [0007](0007-no-standalone-path-for-pinning.md) | pinning에 standalone 경로를 두지 않는다 | Accepted |
| [0008](0008-baseline-tuning-policy.md) | baseline 튜닝 정책과 allocator의 목적 정의 | Accepted |

### Actor Runtime

실행 규약은 [`design/actor-runtime.md`](../actor-runtime.md)에 있다. 아래 셋은
**왜 그 모델을 골랐는지**만 남긴다.

| # | 제목 | Status |
|---|---|---|
| [0010](0010-actor-ownership-and-lifetime.md) | Actor 소유권과 수명 모델 | Accepted |
| [0011](0011-actor-scheduling-and-serialization.md) | Actor 실행 직렬화와 스케줄링 모델 | Accepted |
| [0012](0012-actor-stop-failure-shutdown.md) | Actor 종료·실패·셧다운 semantics | Accepted |
