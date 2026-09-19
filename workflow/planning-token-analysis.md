# Planning 단계 토큰 소비 분석

## 1. 목적

Actor 실행용 worker thread pool 작업에서 Planning 단계의 토큰 소비가 지나치게 커진
원인을 분석하고, Workflow의 검토 품질을 유지하면서 비용을 줄이는 방향을 정리한다.

정확한 단계별 토큰 로그와 당시 Workflow artifact는 남아 있지 않으므로, 이 문서는
현재 Workflow 규칙과 실제 대화 흐름을 근거로 한 구조적 분석이다.

## 2. 결론

가장 큰 문제는 Planning을 꼼꼼하게 수행한 것 자체가 아니라, 긴 Project Context와
상세 설계를 유지한 채 사용자 확인과 Review를 여러 turn으로 나눠 반복 처리한 것이다.

```text
긴 Project Context
  × 구현 수준의 상세 Design
  × 독립적인 전체 Review와 재검토
  × 여러 사용자 승인 turn
  × artifact 및 승인 요약의 내용 중복
```

우선 개선할 대상은 다음 세 가지다.

1. 사용자 결정과 승인을 가능한 한 한 번에 묶는다.
2. Design에는 구현 전에 반드시 확정해야 할 결정만 기록한다.
3. 재검토는 전체 문맥이 아니라 변경분과 미해결 finding만 대상으로 한다.

## 3. 주요 원인

### 3.1 Planning 단계와 사용자 왕복이 너무 세분화됨

현재 Planning은 다음 일곱 하위 단계로 구성된다.

```text
Discovery
  → Specification
  → Specification Confirmation
  → Design
  → Design Review
  → Design Refinement
  → Approval
```

Specification 확인과 최종 Design 승인이 분리되어 있고, Reviewer가 여러 명이면 Reviewer
선택도 사용자에게 별도로 요청한다.

실제 작업에서는 다음 결정이 각각 별도 turn으로 처리되었다.

- 요구사항과 범위 확인
- 별도 AI Reviewer 사용 여부
- Claude Code 선택
- ActorSystem 단일 인스턴스 조건
- 최종 설계 승인

사용자 응답이 짧더라도 각 turn은 앞서 누적된 코드, 프로젝트 문서, 설계와 Review 결과를
다시 문맥으로 처리한다. 긴 문맥을 읽은 뒤의 잦은 왕복이 전체 입력 토큰을 크게 증폭했다.

### 3.2 Design 종료 조건이 구현 수준의 상세함을 요구함

현재 Design은 작업 성격에 따라 다음을 모두 검토하도록 안내한다.

- API와 책임 분리
- ownership과 lifetime
- state transition과 concurrency
- synchronization과 failure semantics
- resource management와 performance
- 확장성, integration, observability, testability

또한 구현자가 새로운 핵심 설계를 즉석에서 만들지 않아도 될 정도로 구체적이어야 한다.

이번 작업은 동시성 컴포넌트였기 때문에 Design이 다음 범위까지 확장되었다.

- 정확한 공개 API
- submission commit point
- shutdown leader와 재진입 처리
- callback admission과 quiescence
- lost-wakeup 방지 절차
- TLS identity
- 통계 소유권 이동
- 파일별 변경 계획
- 상세 테스트 목록

correctness에 필요한 핵심 동시성 결정은 가치가 있었다. 그러나 테스트 전체 목록과 일부
구현 의사코드까지 Planning에서 확정하면서 Design이 사실상 사전 구현 문서가 되었다.

### 3.3 독립 Review가 전체 문맥을 다시 소비함

현재 Reviewer는 다음 자료를 읽을 수 있다.

- 전체 Specification과 Design
- 관련 source code와 test
- project instructions
- 설계 문서와 ADR
- 기타 필요한 Project Context

fresh AI session은 독립성 면에서는 유용하지만 Main AI가 확인한 자료를 다른 context에서
처음부터 다시 읽게 된다. 첫 Review가 Blocking finding을 반환한 뒤 설계가 변경되면서
두 번째 전체 Review도 수행되었다.

독립 Review 자체는 낭비가 아니었다. shutdown fairness, lifetime, lost wakeup 등 실제
설계 결함을 발견했기 때문이다. 문제는 재검토도 전체 설계와 Project Context를 다시 읽는
방식이었다.

### 3.4 Workflow artifact와 승인 요약이 중복됨

Planning은 기본적으로 다음 artifact를 만든다.

- `status.md`
- `spec.md`
- `design.md`
- `review.md`

여기에 Specification Confirmation 응답과 Approval 응답이 내용을 다시 요약한다.
단일 ActorSystem, pool ownership, shutdown 책임과 같은 결정이 여러 artifact와 대화에
반복될 수 있지만, 현재 규칙에는 중복 금지나 문서 크기 제한이 없다.

### 3.5 고정 절차의 초기 문맥 비용이 큼

현재 문서 크기를 기준으로 Planning 관련 Workflow 규칙은 약 16,700자이고,
`CLAUDE.md`는 약 10,300자다. 관련 코드와 설계 문서를 읽기 전에 약 27,000자의 규칙과
프로젝트 문맥이 먼저 들어간다.

한 번 읽는 비용은 감수할 수 있지만, 여러 turn과 fresh Reviewer에서 유사 문맥이
반복되면 전체 소비가 빠르게 커진다.

### 3.6 비용과 반복을 제한하는 종료 규칙이 없음

현재 Workflow에는 다음 제한이 없다.

- Planning 문서의 권장 크기
- 사용자 결정 요청 횟수
- Review 반복 횟수
- 재검토 입력 범위
- 동일 Project Context의 재독 조건
- Planning에 사용할 시간 또는 토큰 예산

따라서 설계가 필요 이상으로 상세해지거나 Review가 반복되어도 이를 멈추고 범위를
축소할 명시적인 기준이 없다.

## 4. 유지해야 할 부분

다음 요소는 비용이 들더라도 유지할 가치가 있다.

- Requirement와 Design decision의 분리
- 구현 전 사용자 승인
- 동시성, 수명, 데이터 손실 위험이 큰 변경의 독립 Review
- Blocking finding 해결 후 Development 진입
- Confirmed, Proposed, Open 상태 구분
- Reviewer's read-only 권한

문제는 이 절차의 존재가 아니라 모든 작업과 모든 재검토에서 최대 범위로 적용되는 점이다.

## 5. 개선 방향

### 5.1 Planning 경로를 위험도에 따라 구분

#### Compact Planning — 기본 경로

```text
Discovery
  → 사용자 결정 질문 일괄 제시
  → 통합 Specification/Design
  → 필요한 경우에만 Review
  → 최종 승인
```

다음 규칙을 적용한다.

- 사용자 질문은 가능한 한 한 번에 묶는다.
- Specification Confirmation을 별도 승인 turn으로 강제하지 않는다.
- Reviewer 선택이 필요하면 다른 결정 질문과 함께 제시한다.
- 최종 승인 한 번으로 Development에 진입한다.
- 기술적으로 AI가 결정할 수 있는 내용은 사용자에게 반복 확인하지 않는다.

#### Deep Planning — 고위험 작업

다음 조건에 해당할 때만 현재 수준의 상세 절차를 사용한다.

- 복잡한 동시성 또는 lock-free 알고리즘
- 데이터 손실이나 복구 불가능한 변경 가능성
- 공개 API 또는 호환성 변경
- 대규모 migration
- 사용자가 상세 설계와 독립 검토를 명시적으로 요청

Deep Planning에서도 사용자 질문은 일괄 처리하고, 재검토는 변경분 중심으로 수행한다.

### 5.2 사용자 승인 지점을 축소

권장 사용자 접점은 최대 두 번이다.

1. 제품 의미나 범위를 바꾸는 Open Question을 한 번에 결정
2. 최종 Specification과 Design을 한 번에 승인

요구사항이 처음부터 충분히 명확하다면 첫 번째 접점도 생략할 수 있다.

### 5.3 Design의 상세도 경계 변경

Design 종료 기준을 다음처럼 바꾼다.

> 구현 전에 되돌리기 어렵거나 여러 파일에 영향을 주는 핵심 결정이 확정되어 있다.

Planning에서 반드시 다룰 내용:

- 공개 API와 외부 동작
- 책임과 ownership
- 핵심 lifetime 및 shutdown 규약
- 중요한 failure semantics
- correctness를 좌우하는 동시성 invariant
- 검증 전략의 핵심

Development로 미룰 수 있는 내용:

- 자명한 private helper 구조
- 기계적인 파일별 수정 목록
- 모든 테스트 케이스의 상세 절차
- 구현 중 자연스럽게 결정할 수 있는 지역 변수와 내부 표현
- 설계 선택에 영향을 주지 않는 의사코드

### 5.4 Review packet을 제한

첫 Review에는 다음만 기본 제공한다.

- Specification 요약
- 핵심 invariant
- 주요 설계 결정표
- 알려진 위험과 Open Question
- Reviewer가 직접 확인할 코드 경로 목록

Reviewer가 필요하다고 판단할 때만 추가 코드나 문서를 읽는다.

재검토에는 다음만 제공한다.

- 변경된 Design 절
- 이전 Blocking/Important finding
- 각 finding에 대한 반영 내용
- 변경으로 새로 생긴 위험

핵심 구조가 완전히 바뀐 경우에만 전체 Review를 다시 수행한다.

### 5.5 Artifact 간 단일 출처를 강화

- `status.md`는 상태와 artifact 링크만 기록한다.
- `spec.md`는 요구사항만 기록하고 프로젝트 설명을 반복하지 않는다.
- `design.md`는 `spec.md`를 복사하지 않고 Requirement ID를 참조한다.
- `review.md`는 설계를 재서술하지 않고 finding과 disposition만 기록한다.
- Approval 응답은 변경점과 결정이 필요한 부분만 보여준다.

### 5.6 기본 예산과 중단 조건 추가

권장 baseline은 다음과 같다.

| 항목 | Compact | Deep |
|---|---:|---:|
| 사용자 결정 요청 | 최대 1회 | 최대 2회 |
| 독립 Review | 필요 시 1회 | 기본 1회 |
| 재검토 | 변경분만 | Blocking 변경분만 |
| `spec.md` | 약 100줄 이내 | 필요 범위 |
| `design.md` | 약 200줄 이내 | 초과 이유 기록 |
| Project Context 재독 | 원칙적으로 금지 | 전제 변경 시에만 |

예산을 넘겨야 한다면 계속 확장하기 전에 다음을 사용자에게 보고한다.

- 왜 추가 분석이 필요한가
- 어느 위험을 줄이는가
- 추가 Review 또는 질문이 실제로 필요한가

## 6. 권장 Planning 흐름

```text
Workflow 진입
  ↓
관련 Project Context 1회 조사
  ↓
위험도 분류: Compact / Deep
  ↓
Requirement 및 사용자 결정점 일괄 정리
  ↓
사용자 판단이 필요한 경우 한 번에 질문
  ↓
Specification + 핵심 Design 작성
  ↓
위험도에 따라 Review 생략 또는 1회 수행
  ↓
Blocking finding만 반영
  ↓
필요하면 변경분만 재검토
  ↓
최종 승인 1회
  ↓
Development
```

## 7. 이번 사례에 적용했어야 할 방식

worker thread pool 작업은 동시성과 shutdown lifetime 위험이 크므로 Deep Planning 대상이다.
다만 다음처럼 축소할 수 있었다.

1. Discovery에서 기존 scheduler, ActorSystem lifecycle, 미래 확장 요구만 확인한다.
2. 사용자에게 다음 결정만 한 번에 질문한다.
   - pool lifecycle을 호출자가 소유하는가
   - ActorSystem은 단일 인스턴스인가
   - 독립 Reviewer로 Claude Code를 사용할 것인가
3. 공개 API, ownership, terminal callback, shutdown invariant만 Design에 확정한다.
4. Claude Reviewer가 전체 저장소가 아니라 핵심 설계와 관련 코드 경로만 읽는다.
5. 첫 Review의 Blocking finding을 반영한 뒤 변경 절만 재검토한다.
6. 최종 요약 한 번으로 사용자 승인을 받는다.

이 방식이면 독립 Review의 correctness 이점은 유지하면서 대화 turn, 문서 중복, 전체
재검토 비용을 크게 줄일 수 있다.

