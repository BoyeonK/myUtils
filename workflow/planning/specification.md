# Specification

Specification은 사용자가 **무엇을 필요로 하는지** 명확하게 정의하고 확인하는 과정이다.

이 문서는 다음 두 하위 단계를 다룬다.

```text
Specification
   ↓
Specification Confirmation
```

---

# 1. Specification

## 목적

사용자의 초기 요청을 **무엇을 만들어야 하는지 판단 가능한 명시적 요구사항**으로 발전시킨다.

Specification은 AI가 필요한 기능을 대신 결정하는 단계가 아니다.

AI는 다음 역할을 수행한다.

- 사용자의 요구를 구조화한다.
- 모호한 부분을 발견한다.
- 빠진 요구사항이나 결정점을 발견한다.
- 기존 Project Context에서 이미 정해진 제약을 찾아낸다.
- 필요한 경우 선택지를 설명한다.
- 사용자의 결정을 명문화한다.

최종적으로 무엇이 필요한지는 사용자가 결정한다.

Specification이 도달해야 하는 상태는 다음과 같다.

> Design 단계에서 AI가 사용자 의도를 추측할 필요가 없다.

질문의 개수는 목표가 아니다.

Specification의 결과는 다음 위치에 기록한다.

```text
.workflow/tasks/<task-id>/spec.md
```

---

## 기본 구조

```markdown
# Specification

## Problem

## Goals

## Use Cases

## Requirements

## Constraints

## Non-goals

## Acceptance Criteria

## Open Questions

## Related Project Context
```

필요하지 않은 항목은 생략하거나 구조를 조정할 수 있다.

형식을 채우는 것보다 요구사항을 명확하게 만드는 것이 우선이다.

---

## Requirements와 Design의 분리

Specification은 가능한 한 **무엇이 필요한가**를 정의한다.

구체적인 구현 방법은 Design에서 다룬다.

단, 특정 구현 방식 자체가 사용자의 명시적 요구이거나
Project Context에서 이미 확정된 제약이라면
Specification의 Requirement 또는 Constraint에 포함할 수 있다.

---

## AI의 제안

AI는 요구사항을 구체화하기 위해 새로운 가능성이나 질문을 제안할 수 있다.

그러나 사용자가 요구하지 않았고 Project Context에서도 확정되지 않은 내용을
Requirement, Constraint, Non-goal 또는 Acceptance Criteria로 자동 확정하지 않는다.

AI가 제안하거나 아직 결정되지 않은 내용은 Workflow의 정보 상태에 따라
`Proposed` 또는 `Open`으로 구분한다.

baseline에 필요한 항목의 처리는 아래 `사용자 질문`의 출처 확인 규칙을 따른다.

---

## 사용자 질문

Specification은 사용자의 요구를 확인하는 인터뷰에 가깝다.

필요한 범위에서 다음을 적극적으로 확인한다.

- 무엇을 가능하게 만들고 싶은가
- 어떤 사용 상황을 예상하는가
- 반드시 보장해야 하는 동작은 무엇인가
- 허용 가능한 것과 허용하지 않는 것은 무엇인가
- 실패했을 때 기대하는 동작은 무엇인가
- 기존 동작 중 반드시 유지해야 하는 것은 무엇인가
- 이번 작업에서 명시적으로 하지 않을 것은 무엇인가
- 완료되었다고 판단할 수 있는 조건은 무엇인가

반면 구현 방법의 선택은 Specification에서 사용자에게 떠넘기지 않는다.

어떤 동기화 도구를 사용할지, 어떤 컨테이너나 소유권 표현을 사용할지 같은 결정은 Design의 책임이다.

### 출처 확인

Specification baseline에 포함하는 Requirement, Constraint, Non-goal, Acceptance Criteria는
각각 출처가 있어야 한다.

출처는 사용자의 발언 또는 Project Context에서 확인된 기존 제약과 결정이다.

baseline에 필요한 항목인데 출처가 없다면
AI가 임의로 확정하지 않고 사용자에게 질문한다.

반면 현재 작업에 필수적이지 않은 AI의 아이디어는 질문을 강제하지 않는다.

이러한 내용은 `Proposed`로 남기거나 Specification에서 제외한다.

```text
baseline 항목에 출처가 있는가?

출처 있음
   → 그대로 사용

출처 없음
   ├─ baseline에 필요함 → 사용자에게 질문
   └─ 단순 아이디어     → Proposed 또는 제외
```

`Proposed` 표시는 확인을 생략하기 위한 수단이 아니다.

baseline에 필요한 항목을 `Proposed`로 표시한 뒤
사용자 확인 없이 Specification Confirmation의 요약에 포함하지 않는다.

### 질문 형식

질문에는 가능한 한 다음을 함께 제공한다.

- 무엇을 결정해야 하는가
- 왜 결정이 필요한가
- 가능한 선택지
- 각 선택지가 결과를 어떻게 바꾸는가
- Main AI가 제안하는 기본안

단순한 찬반 확인보다 **결정의 의미와 선택지**를 보여준다.

Main AI의 기본안은 사용자 결정과 구분한다.

사용자 판단이 필요한 사항이 여러 개라면 가능한 한 하나의 묶음으로 함께 제시한다.

### 질문하지 않는 것

다음은 질문하지 않는다.

- 기존 Project Context에서 이미 답을 찾을 수 있는 사항
- Design 단계에서 결정할 구현 방법
- Design 선택이나 Acceptance Criteria를 바꾸지 않는 세부 사항

---

## Acceptance Criteria

Acceptance Criteria는 구현 완료 여부를 실제로 판단할 수 있어야 한다.

가능하면 관찰 가능한 동작이나 검증 가능한 조건으로 작성한다.

---

## 종료 조건

다음 조건을 만족하면 Specification Confirmation으로 이동한다.

- 문제와 목표가 명확하다.
- 주요 사용 사례가 정의되어 있다.
- 요구사항과 비요구사항의 경계가 보인다.
- 주요 제약이 확인되어 있다.
- Requirement와 Design decision이 가능한 범위에서 분리되어 있다.
- 완료 여부를 검증할 Acceptance Criteria가 있다.
- baseline의 각 Requirement, Constraint, Non-goal, Acceptance Criteria에 출처가 있다.
- Design에서 사용자 의도를 추측해야 하는 항목이 남아 있지 않다.
- 아직 확정되지 않은 사항이 Open Question으로 드러나 있다.

이 시점의 `spec.md`는 사용자 확인 전 초안이다.

---

# 2. Specification Confirmation

## 목적

`spec.md`가 **사용자가 실제로 만들고 싶은 것을 올바르게 표현하고 있는지** 확인한다.

이 단계는 AI의 독립적인 기술 검토 단계가 아니라 사용자 의도에 대한 확인 단계다.

핵심 질문은 다음과 같다.

> 이 Specification이 내가 필요로 하는 것을 제대로 표현하고 있는가?

---

## 수행할 작업

Main AI는 사용자에게 Specification의 핵심 내용을 요약한다.

최소한 다음을 포함한다.

```text
[Workflow: Planning / Specification Confirmation]

작업 목표

주요 Use Case

Requirements

Constraints

Non-goals

Acceptance Criteria

남아 있는 Open Question
```

모든 `spec.md` 내용을 그대로 다시 출력할 필요는 없다.

사용자가 요구사항을 판단하는 데 필요한 내용만 명확하게 보여준다.

---

## 사용자 수정

사용자는 필요에 따라 다음과 같은 내용을 수정할 수 있다.

- Requirement 추가 또는 제거
- 범위 수정
- Constraint 수정
- Non-goal 수정
- Acceptance Criteria 수정
- Open Question에 대한 결정

Main AI는 사용자 피드백을 `spec.md`에 반영한다.

큰 변경이 발생해 Project Context를 다시 확인해야 한다면
Discovery 또는 Specification으로 돌아갈 수 있다.

---

## 확인

다음과 같은 의미의 사용자 응답을 Specification 확인으로 간주할 수 있다.

- 이 요구사항이 맞다.
- 이 범위로 진행하자.
- Specification 확정.
- Design으로 진행.
- 이에 준하는 명확한 표현

확인 표현은 현재 Workflow 단계의 의미로 해석한다.

Specification Confirmation 단계의 확인을
최종 Design 승인이나 Development 진입 승인으로 확대 해석하지 않는다.

---

## 재확인

이미 확인된 Specification이 이후 단계에서 변경된 경우
변경 내용이 사용자 의도나 작업 범위에 영향을 주는지 확인한다.

다음 항목의 **내용이 변경된 경우에는 기본적으로 재확인 대상으로 취급한다.**

- Requirements
- Non-goals
- Acceptance Criteria

표현이나 서술만 바뀌고 위 항목의 의미가 그대로인 경우에는 별도의 재확인을 요구하지 않는다.

재확인이 필요한 경우 전체 Specification을 다시 제시하기보다
변경된 부분과 그 영향만 중심으로 사용자에게 확인받는다.

---

## 종료 조건

다음을 만족하면 Design으로 이동한다.

- 사용자가 핵심 요구사항과 범위를 확인했다.
- Design을 막는 Requirement 수준의 Open Question이 없다.
- `spec.md`가 현재 작업의 요구사항 baseline으로 사용할 수 있다.

Specification Confirmation 이후의 `spec.md`가 Design의 기준이 된다.

확인 상태와 중요한 사용자 결정은 필요한 범위에서 `status.md`에 반영한다.
