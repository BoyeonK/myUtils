# Design Review, Refinement and Approval

이 문서는 Planning 후반부의 다음 하위 단계를 다룬다.

```text
Design Review
   ↓
Design Refinement
   ↓
Approval
```

---

# 1. Design Review

## 목적

설계 작성자의 관점에 묶이지 않은 Reviewer가 `design.md`를 비판적으로 검토한다.

목표는 설계를 정당화하는 것이 아니라 **문제를 발견하고 더 나은 설계 가능성을 찾는 것**이다.

Reviewer가 반환한 검토 결과는 Main AI가 다음 위치에 기록한다.

```text
.workflow/tasks/<task-id>/review.md
```

---

## Reviewer 선택

Design Review를 시작하기 전에 사용할 Reviewer를 결정한다.

Reviewer는 다음과 같은 형태일 수 있다.

- 별도의 subagent
- 프로젝트에 정의된 custom agent
- 별도의 AI session
- 외부 AI 도구 또는 model
- 그 밖에 독립적인 검토가 가능한 실행 환경

현재 작업 또는 프로젝트에 이미 유효한 Reviewer 선택이 있다면 이를 재사용할 수 있다.

사용 가능한 Reviewer가 둘 이상이고 기존 선택이 없다면
Main AI는 사용 가능한 선택지를 사용자에게 제시하고 선택을 요청한다.

Reviewer 선택 외에도 사용자 판단이 필요한 Open Question이 있다면 가능한 한 하나의 질문으로 함께 제시한다.

현재 Workflow에서 이미 Reviewer가 선택되었고 그 선택이 여전히 유효하다면 동일한 결정을 반복해서 요청하지 않는다.

사용자가 특정 Reviewer를 지정했다면 가능한 경우 해당 Reviewer를 사용한다.

선택한 Reviewer를 사용할 수 없는 경우 임의로 다른 Reviewer로 대체하지 않고 사용자에게 알린다.

Reviewer의 구체적인 구현 방식이나 AI vendor는 Workflow 자체의 일부로 고정하지 않는다.

선택된 Reviewer는 필요한 범위에서 `status.md`에 기록한다.

---

## Reviewer 실행 권한

Reviewer는 **읽기 전용으로 실행한다.**

Reviewer는 검토에 필요한 범위에서 다음 자료를 읽을 수 있다.

- 확정된 `spec.md`
- 현재 `design.md`
- 관련 source code와 test
- 관련 project instructions
- 설계 및 architecture 문서
- 기존 decision record
- 그 밖에 검토에 필요한 Project Context

Reviewer는 다음과 같은 변경 작업을 수행하지 않는다.

- source code 수정
- test 수정
- Specification 수정
- Design 수정
- Workflow artifact 수정
- repository 상태 변경
- 구현 시작
- commit 또는 그 밖의 project mutation

Reviewer의 역할은 자료를 읽고 분석하여 **검토 결과를 Main AI에게 반환하는 것**까지다.

`review.md`를 기록하고 Workflow 상태를 변경하는 책임은 Main AI에게 있다.

사용하는 Reviewer 실행 환경이 권한 제한을 지원한다면
가능한 범위에서 실제 실행 권한도 read-only로 제한한다.

---

## 독립성

가능하면 Main AI는 Design Review를 별도의 AI agent/session에 위임한다.

독립성은 다음과 같은 방법으로 확보할 수 있다.

- 별도의 subagent
- 별도의 AI session
- 별도의 model
- 설계 과정의 reasoning을 공유하지 않은 fresh context

Main AI가 별도의 AI 실행 컨텍스트를 직접 생성할 수 있다면
선택된 Reviewer를 실행하고 결과를 수집한다.

### Reviewer를 사용할 수 없는 경우

애초에 독립적인 Reviewer 실행 환경을 사용할 수 없는 경우에는
Main AI가 fresh review 역할을 수행할 수 있다.

이 경우 완전한 독립 검토가 아니었음을 명시한다.

반면 이미 특정 Reviewer가 선택된 뒤 해당 Reviewer를 사용할 수 없게 된 경우에는
Main AI가 임의로 다른 Reviewer나 self-review로 대체하지 않는다.

이 경우 사용자에게 상황을 알리고 다음 처리 방향을 결정한다.

---

## Reviewer 입력

Reviewer는 우선 다음 자료를 중심으로 검토를 시작한다.

- 확정된 `spec.md`
- 현재 `design.md`
- 검토에 필요한 최소한의 Project Context

추가 source code, test, 설계 문서 또는 decision record는
특정 판단을 검증하기 위해 필요한 경우에만 조회한다.

관련 가능성이 있다는 이유만으로 repository 전체나 관련 문서 전체를 선제적으로 탐색하지 않는다.

불필요하게 Designer의 설계 정당화 과정이나 이전 대화 전체를 제공하지 않는다.

Reviewer가 결과물과 필요한 Project Context를 중심으로 판단하도록 한다.

---

## Reviewer 역할

Reviewer는 구현을 시작하지 않는다.

다음을 적극적으로 검토한다.

### Specification 충족

- 주요 Requirement를 모두 다루는가
- Design이 Requirement의 의미를 바꾸지 않았는가
- Acceptance Criteria를 실제로 구현하고 검증할 수 있는가

### API와 계약

- API의 의미가 명확한가
- 호출자와 구현자의 책임이 구분되는가
- failure semantics가 명확한가

### Ownership과 Lifetime

관련된 경우 다음을 검토한다.

- 소유권이 명확한가
- 객체의 수명 관계가 명확한가
- 파괴 순서가 안전한가
- dangling reference 가능성이 없는가
- shutdown과 cleanup 책임이 명확한가

### Concurrency

관련된 경우 다음을 검토한다.

- race condition
- deadlock
- ordering 문제
- lost wakeup
- concurrent shutdown
- synchronization 범위
- resource lifetime

### 복잡도

- 요구사항보다 과도하게 복잡하지 않은가
- 불필요한 abstraction이 추가되지 않았는가
- 더 단순한 대안이 존재하는가

### Project Integration

- 기존 프로젝트 구조와 충돌하지 않는가
- 기존 설계 결정과 모순되지 않는가
- 기존 component와 책임이 불필요하게 중복되지 않는가
- 프로젝트의 기존 convention과 지나치게 어긋나지 않는가

### Testability

관련된 경우 다음을 검토한다.

- 핵심 동작을 검증할 수 있는가
- 중요한 invariant를 관찰할 수 있는가
- 검증하기 어려운 암묵적 계약이 존재하지 않는가

---

## Finding 분류

각 finding은 **Severity**와 **Status**를 분리해서 기록한다.

### Severity

#### Blocking

Development 전에 반드시 해결하거나 명시적인 Waiver가 필요한 문제.

#### Important

Development 전에 충분한 검토와 처리 방향 결정이 필요한 문제.

#### Suggestion

필수는 아니지만 설계를 개선할 수 있는 제안.

### Status

#### Open

아직 처리되지 않은 finding.

#### Resolved

Design 변경 또는 추가 확인을 통해 해결된 finding.

#### Waived

문제가 남아 있음을 인지한 상태에서 사용자가 명시적으로 위험을 수용한 finding.

Severity와 Status는 서로 다른 축이다.

예:

```text
Severity: Blocking
Status: Waived
```

---

## Waiver 규칙

`Waived`는 finding 단위로만 적용한다.

Waiver는 해결 시도를 대신하는 우회 경로가 아니다.

Blocking finding은 기본적으로 해결한다.

해결이 불가능하거나 해결 비용이 과도하다는 근거가 있고,
사용자가 해당 위험과 영향을 확인한 경우에 한해 Waive할 수 있다.

Main AI는 사용자가 위험을 수용하려는 경우
대상 finding과 남아 있는 위험을 명확하게 제시하고
해당 finding을 Waive할 것인지 확인한다.

사용자의 응답이 단순한 진행 의사만 나타내고
특정 finding의 위험 수용 여부가 명확하지 않다면
이를 Waiver로 해석하지 않는다.

여러 finding이 남아 있는 경우 각각의 disposition을 구분한다.

남아 있는 모든 finding을 암묵적으로 일괄 Waive하지 않는다.

Waive된 finding은 `review.md`에 최소한 다음을 기록한다.

- 대상 finding
- Severity
- 남아 있는 위험
- 사용자의 명시적 Waiver 결정
- 필요한 경우 Waiver 이유

Waived finding은 `status.md`에도 알려진 위험으로 유지한다.

Waiver는 Acceptance Criteria 충족이나 실제 검증 실패를 성공으로 바꾸는 근거로 사용하지 않는다.

---

## Reviewer의 역할 제한

Reviewer는 최종 설계를 직접 확정하지 않는다.

Reviewer의 책임은 문제와 대안을 발견하고 검토 결과를 반환하는 것이다.

Reviewer는 검토 과정에서 발견한 문제를 직접 수정하지 않는다.

최종 반영 여부는 Main AI가 Design Refinement에서 판단하고,
중요한 선택은 필요하면 사용자에게 제시한다.

---

## 종료 조건

Reviewer의 검토 결과가 반환되고,
Main AI가 해당 결과를 `review.md`에 기록하면
Design Refinement로 이동한다.

각 Review가 완료될 때 Main AI는 현재 Review round를 `status.md`에 기록한다.

---

# 2. Design Refinement

## 목적

Design Review 결과를 검토하고 필요한 내용을 설계에 반영한다.

Reviewer의 모든 의견을 자동으로 받아들이지 않는다.

각 주요 의견에 대해 다음 중 하나를 결정한다.

- 반영
- 부분 반영
- 반영하지 않음

필요하면 판단 이유를 남긴다.

---

## 수행할 작업

필요에 따라 `design.md`를 수정한다.

Review 과정에서 문제가 Requirement 자체에 있는 것으로 확인되면
Specification으로 돌아갈 수 있다.

Specification이 변경된 경우 `workflow/planning/specification.md`의 재확인 규칙을 따른다.

사용자 의도의 재확인이 필요하면 Specification Confirmation으로 돌아간다.

새로운 Project Context가 발견되어 기존 전제가 무너졌다면 Discovery까지 돌아갈 수 있다.

---

## 사용자 판단

기술적인 분석과 Review 이후에도 중요한 trade-off가 남아 있고
그 선택이 사용자 의도나 우선순위에 따라 달라진다면 사용자에게 판단을 요청한다.

가능하면 다음을 함께 제공한다.

- 무엇을 결정해야 하는가
- 왜 결정이 필요한가
- 가능한 선택지
- 각 선택지의 주요 trade-off
- Main AI가 제안하는 기본안

Main AI의 기본안은 사용자 결정과 구분한다.

---

## 재Review 진입

Blocking 또는 Important finding을 반영하여
Design의 의미가 변경된 경우 재Review를 수행한다.

Suggestion만 반영했거나,
설계 의미가 바뀌지 않는 표현·정리 수준의 수정만 있었다면
재Review 없이 Approval로 이동할 수 있다.

finding을 반영하지 않기로 결정했고 Design도 변경되지 않은 경우에는
그 판단 근거를 기록하며 자동으로 재Review를 요구하지 않는다.

---

## 반복 Review

재검토는 기본적으로 다음만 대상으로 한다.

- 이전 Review 이후 변경된 Design
- 기존 Blocking 또는 Important finding
- 각 finding에 대한 반영 내용
- 변경으로 인해 새로 발생한 위험

Reviewer는 재검토에 필요한 범위만 다시 확인한다.

Design의 핵심 구조나 전제가 크게 변경된 경우에만 전체 Review를 다시 수행한다.

재검토의 목적은 이전 Blocking/Important finding의 해결 여부와
변경으로 인해 새롭게 발생한 중대한 문제를 확인하는 것이다.

새 Suggestion만 발견된 경우에는 추가 Review cycle을 시작하지 않는다.

---

## Review round와 escalation

Review round는 같은 Specification/Design baseline에 대한
Review와 Refinement 반복 횟수를 의미한다.

각 Review 완료 시 현재 round를 `status.md`에 기록한다.

Specification 또는 Design의 핵심 전제가 변경되어
새로운 전체 Review가 필요한 경우에는 새로운 Review cycle로 취급한다.

이 경우 Review round를 다시 1부터 시작하고,
새 cycle이 시작되었다는 사실을 `status.md`에 기록한다.

세 번째 이후의 Review round에서 새로운 Open Blocking 또는 Important finding이 발생하면
추가 Review를 자동으로 계속하지 않는다.

Main AI는 남아 있는 finding, 위험, 현재 선택지와 주요 trade-off를 정리해 사용자에게 제시하고
다음 처리 방향을 결정한다.

이는 Review를 강제로 종료하기 위한 횟수 제한이 아니라,
Review가 반복적으로 수렴하지 않을 때 사용자를 호출하는 escalation trigger다.

Escalation 이후에도 finding은 `Resolved` 또는 명시적인 `Waived` 상태가 되기 전까지 Open으로 유지한다.

---

## 종료 조건

다음을 만족하면 Approval로 이동한다.

- Open 상태의 Blocking finding이 없다.
- Important finding의 처리 상태가 결정되었다.
- Specification과 Design이 서로 일치한다.
- Project Context와 알려진 충돌이 없다.
- 구현 전에 확정해야 할 핵심 설계 결정이 정리되어 있다.
- 남은 결정은 Development에서 처리 가능한 구현 세부사항이다.
- 남아 있는 trade-off와 Open Question이 명확하다.
- Waived finding이 있다면 `review.md`와 `status.md`에 기록되어 있다.

---

# 3. Approval

## 목적

사용자가 Development의 기준이 될 최종 Specification과 Design을 확인한다.

Specification Confirmation이

> 무엇을 만들 것인가

에 대한 확인이었다면,

Approval은

> 이 요구사항을 이 설계로 구현할 것인가

에 대한 최종 승인이다.

---

## 사용자에게 제공할 내용

응답은 다음과 같이 시작한다.

```text
[Workflow: Planning / Approval]
```

최소한 다음 내용을 요약한다.

```text
작업 목표

확정된 주요 요구사항

프로젝트의 주요 관련 제약

선택한 설계

검토한 주요 대안

Design Review에서 발견된 주요 문제

Review 반영 결과

Waived finding과 남아 있는 risk

주요 trade-off

남아 있는 Open Question
```

모든 Workflow artifact 내용을 다시 출력할 필요는 없다.

사용자가 최종 판단하는 데 필요한 핵심을 제공한다.

---

## 승인

사용자의 명시적인 승인 전에는 실제 구현을 시작하지 않는다.

승인 표현은 현재 Workflow 단계와 문맥을 기준으로 해석한다.

Specification Confirmation 등 이전 단계의 확인을
최종 Approval로 확대 해석하지 않는다.

### 기존 Approval의 유효성

이전에 승인된 `spec.md`와 `design.md`가 변경되지 않았다면
기존 Approval은 여전히 유효하며 다시 승인을 요청하지 않는다.

승인 이후 `spec.md` 또는 `design.md`의 구현 기준 의미가 변경된 경우에는
현재 내용을 다시 제시하고 Approval을 다시 받는다.

Approval이 무효화된 경우 `status.md`의 `Approval` 값을 현재 확인 상태에 맞게 되돌린다.

예를 들어 Design Approval만 무효화되고 Specification 확인은 여전히 유효하다면
`spec-confirmed`로 되돌릴 수 있으며,
Specification도 다시 확인해야 한다면 `none`으로 되돌린다.

표현 정리나 의미가 바뀌지 않는 편집만으로 기존 Approval을 무효화하지 않는다.

승인 후:

```text
Planning
   ↓
Development
```

로 이동한다.

승인 시점의 `spec.md`와 `design.md`가 Development의 구현 기준이 된다.

Approval 상태와 Development 진입 여부를 `status.md`에 반영한다.
