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
`Superseded by ADR-00XX`로 바꾼다. 기존 문서 본문은 고치지 않는다.

## 목록

| # | 제목 | Status |
|---|---|---|
| [0001](0001-refcounted-arena.md) | SendBuffer를 refcounted arena + bump allocation으로 | Accepted |
| [0002](0002-sendbuffer-sendview-split.md) | SendBuffer / SendView 타입 분리 | Accepted |
| [0003](0003-chunk-lifetime-shared-ptr.md) | chunk 수명을 shared_ptr로 관리 | Accepted |
| [0004](0004-single-global-chunkpool.md) | 단일 global ChunkPool과 그 수명 | Accepted |
| [0005](0005-reserve-commit-tail-reclaim.md) | Reserve 단일 진입점과 tail reclaim 조건 | Accepted |
| [0006](0006-size-validation-and-alignment.md) | 크기 검증 계층, 실패 정책, alignment 제외 | Accepted |
