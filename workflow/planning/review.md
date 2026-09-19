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

* 별도의 subagent
* 프로젝트에 정의된 custom agent
* 별도의 AI session
* 외부 AI 도구 또는 model
* 그 밖에 독립적인 검토가 가능한 실행 환경

사용 가능한 Reviewer가 둘 이상이라면 Main AI는 사용 가능한 선택지를 사용자에게 제시하고 선택을 요청한다.

사용자가 특정 Reviewer를 지정했다면 가능한 경우 해당 Reviewer를 사용한다.

선택한 Reviewer를 사용할 수 없는 경우 임의로 다른 Reviewer로 대체하지 않고 사용자에게 알린다.

Reviewer의 구체적인 구현 방식이나 AI vendor는 Workflow 자체의 일부로 고정하지 않는다.

---

## Reviewer 실행 권한

Reviewer는 **읽기 전용으로 실행한다.**

Reviewer는 검토에 필요한 범위에서 다음 자료를 읽을 수 있다.

* 확정된 `spec.md`
* 현재 `design.md`
* 관련 source code와 test
* 관련 project instructions
* 설계 및 architecture 문서
* 기존 decision record
* 그 밖에 검토에 필요한 Project Context

Reviewer는 다음과 같은 변경 작업을 수행하지 않는다.

* source code 수정
* test 수정
* Specification 수정
* Design 수정
* Workflow artifact 수정
* repository 상태 변경
* 구현 시작
* commit 또는 그 밖의 project mutation

Reviewer의 역할은 자료를 읽고 분석하여 **검토 결과를 Main AI에게 반환하는 것**까지다.

`review.md`를 기록하고 Workflow 상태를 변경하는 책임은 Main AI에게 있다.

사용하는 Reviewer 실행 환경이 권한 제한을 지원한다면
가능한 범위에서 실제 실행 권한도 read-only로 제한한다.

---

## 독립성

가능하면 Main AI는 Design Review를 별도의 AI agent/session에 위임한다.

예:

* 별도의 subagent
* 별도의 AI session
* 별도의 model
* 설계 과정의 reasoning을 공유하지 않은 fresh context

Main AI가 별도의 AI 실행 컨텍스트를 직접 생성할 수 있다면
선택된 Reviewer를 실행하고 결과를 수집한다.

독립적인 AI 실행 환경을 사용할 수 없다면 Main AI가 fresh review 역할을 수행할 수 있지만,
그 경우 완전한 독립 검토가 아니었음을 명시한다.

---

## Reviewer 입력

Reviewer에게 최소한 다음을 제공한다.

* 확정된 `spec.md`
* 현재 `design.md`
* 설계를 평가하는 데 필요한 Project Context

필요하면 실제 repository의 코드와 문서를 읽도록 할 수 있다.

불필요하게 Designer의 설계 정당화 과정이나 이전 대화 전체를 제공하지 않는다.

Reviewer가 결과물과 실제 Project Context를 중심으로 판단하도록 한다.

---

## Reviewer 역할

Reviewer는 구현을 시작하지 않는다.

다음을 적극적으로 검토한다.

### Specification 충족

* 주요 Requirement를 모두 다루는가
* Design이 Requirement의 의미를 바꾸지 않았는가
* Acceptance Criteria를 실제로 구현하고 검증할 수 있는가

### API와 계약

* API의 의미가 명확한가
* 호출자와 구현자의 책임이 구분되는가
* failure semantics가 명확한가

### Ownership과 Lifetime

* 소유권이 명확한가
* 객체의 수명 관계가 명확한가
* 파괴 순서가 안전한가
* dangling reference 가능성이 없는가
* shutdown과 cleanup 책임이 명확한가

### Concurrency

관련된 경우 다음을 검토한다.

* race condition
* deadlock
* ordering 문제
* lost wakeup
* concurrent shutdown
* synchronization 범위
* resource lifetime

### 복잡도

* 요구사항보다 과도하게 복잡하지 않은가
* 불필요한 abstraction이 추가되지 않았는가
* 더 단순한 대안이 존재하는가

### Project Integration

* 기존 프로젝트 구조와 충돌하지 않는가
* 기존 설계 결정과 모순되지 않는가
* 기존 component와 책임이 불필요하게 중복되지 않는가
* 프로젝트의 기존 convention과 지나치게 어긋나지 않는가

### Testability

* 핵심 동작을 검증할 수 있는가
* 중요한 invariant를 관찰할 수 있는가
* 검증하기 어려운 암묵적 계약이 존재하지 않는가

---

## Review 결과 분류

각 지적 사항은 가능하면 중요도를 구분한다.

### Blocking

Development 전에 반드시 해결해야 하는 문제.

### Important

Development 전에 충분한 검토가 필요한 문제.

### Suggestion

필수는 아니지만 설계를 개선할 수 있는 제안.

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

---

# 2. Design Refinement

## 목적

Design Review 결과를 검토하고 필요한 내용을 설계에 반영한다.

Reviewer의 모든 의견을 자동으로 받아들이지 않는다.

각 주요 의견에 대해 다음 중 하나를 결정한다.

* 반영
* 부분 반영
* 반영하지 않음

필요하면 판단 이유를 남긴다.

---

## 수행할 작업

필요에 따라 `design.md`를 수정한다.

Review 과정에서 문제가 Requirement 자체에 있는 것으로 확인되면
Specification으로 돌아갈 수 있다.

사용자 의도의 재확인이 필요하면 Specification Confirmation으로 돌아간다.

새로운 Project Context가 발견되어 기존 전제가 무너졌다면 Discovery까지 돌아갈 수 있다.

---

## 사용자 판단

기술적인 분석과 Review 이후에도 중요한 trade-off가 남아 있고
그 선택이 사용자 의도나 우선순위에 따라 달라진다면 사용자에게 판단을 요청한다.

가능하면 다음을 함께 제공한다.

* 무엇을 결정해야 하는가
* 왜 결정이 필요한가
* 가능한 선택지
* 각 선택지의 주요 trade-off
* Main AI가 제안하는 기본안

Main AI의 기본안은 사용자 결정과 구분한다.

---

## 반복 Review

Blocking issue를 해결하는 과정에서 Design의 핵심 구조가 크게 변경되었다면
Design Review를 다시 수행할 수 있다.

사소한 수정까지 매번 Review를 반복할 필요는 없다.

---

## 종료 조건

다음을 만족하면 Approval로 이동한다.

* Blocking issue가 해결되었다.
* Important issue가 검토되었다.
* Specification과 Design이 서로 일치한다.
* Project Context와 알려진 충돌이 없다.
* Design이 구현 기준으로 사용할 수 있는 수준으로 구체적이다.
* 남아 있는 trade-off와 Open Question이 명확하다.

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

주요 trade-off

남아 있는 Open Question
```

모든 Workflow artifact 내용을 다시 출력할 필요는 없다.

사용자가 최종 판단하는 데 필요한 핵심을 제공한다.

---

## 승인

사용자의 명시적인 승인 전에는 실제 구현을 시작하지 않는다.

승인 후:

```text
Planning
   ↓
Development
```

로 이동한다.

승인 시점의 `spec.md`와 `design.md`가 Development의 구현 기준이 된다.
