# Design

## 목적

확정된 Specification을 만족시키는 기술적 해결 방법을 설계한다.

이 단계에서는 AI가 기술적 판단과 대안 탐색에 적극적으로 개입한다.

결과는 다음 위치에 기록한다.

```text
.workflow/tasks/<task-id>/design.md
```

---

## 설계 원칙

바로 하나의 해결책을 확정하지 않는다.

의미 있는 대안이 존재한다면 후보를 식별하고 비교한다.

모든 문제에 억지로 여러 대안을 만들 필요는 없다.

실질적으로 하나의 접근만 타당하다면 그 이유를 설명한다.

---

## Project Context 활용

Design은 현재 프로젝트에서 확인한 실제 구조와 제약을 활용한다.

필요하면 다음을 직접 참조할 수 있다.

* 실제 클래스와 함수
* 기존 API
* 관련 component
* repository 구조
* 기존 설계 및 아키텍처 문서
* 기존 의사결정 기록
* 관련 테스트
* coding/naming convention

현재 프로젝트에 특화된 Design이 만들어지는 것은 정상이다.

---

## 고려 대상

작업 성격에 따라 필요한 항목을 검토한다.

* API
* 책임 분리
* ownership
* lifetime
* state transition
* concurrency
* synchronization
* error handling
* failure semantics
* resource management
* performance 특성
* 확장 가능성
* 기존 시스템과의 integration
* observability
* testability
* 구현 복잡도

관련 없는 항목은 억지로 포함하지 않는다.

---

## 주요 설계 결정

중요한 선택에는 가능하면 다음을 남긴다.

```text
선택

선택 이유

검토한 대안

주요 trade-off
```

Design Review 이전의 설계안은 구현 기준으로 확정된 것으로 취급하지 않는다.

---

## 사용자 개입

기술적으로 AI가 판단할 수 있는 설계 사항을 불필요하게 사용자에게 모두 떠넘기지 않는다.

AI가 대안을 분석하고 합리적인 기본안을 제시할 수 있다면 먼저 그렇게 한다.

다만 다음과 같은 경우에는 사용자의 판단을 요청할 수 있다.

* 선택에 따라 제품 또는 기능의 의미가 달라지는 경우
* 서로 다른 trade-off 중 사용자의 우선순위가 필요한 경우
* Specification만으로 의도를 판단할 수 없는 경우
* 기존 프로젝트의 중요한 설계 방향을 변경하는 경우

---

## 종료 조건

다음을 만족하면 Design Review로 이동한다.

* Specification의 주요 요구사항을 모두 다룬다.
* 기존 프로젝트와의 관계가 설명되어 있다.
* 핵심 API와 동작 의미가 설명되어 있다.
* 주요 ownership과 lifetime이 설명되어 있다.
* 주요 failure path가 설명되어 있다.
* 중요한 trade-off가 드러나 있다.
* 구현자가 새로운 핵심 설계를 즉석에서 만들지 않아도 될 정도로 구체적이다.

이 시점의 `design.md`는 검토 전 설계안이다.
