# Planning

Planning은 사용자의 초기 요청을 구현 가능한 요구사항과 검토된 설계로 발전시키는 단계다.

Planning에서는 실제 코드를 구현하지 않는다.

Planning은 다음 하위 단계로 진행한다.

```text
Discovery
   ↓
Specification
   ↓
Specification Confirmation
   ↓
Design
   ↓
Design Review
   ↓
Design Refinement
   ↓
Approval
```

Specification에서는 사용자가 무엇을 필요로 하는지 명확하게 만든다.

Design에서는 확정된 Specification을 바탕으로 어떻게 구현할 것인지 결정한다.

이 둘의 의사결정 주체와 목적을 혼동하지 않는다.

---

## 하위 단계 문서

각 하위 단계에 진입하면 해당 작업을 수행하기 전에 관련 문서를 읽는다.

| 하위 단계 | 문서 |
|---|---|
| Discovery | `workflow/planning/discovery.md` |
| Specification | `workflow/planning/specification.md` |
| Specification Confirmation | `workflow/planning/specification.md` |
| Design | `workflow/planning/design.md` |
| Design Review | `workflow/planning/review.md` |
| Design Refinement | `workflow/planning/review.md` |
| Approval | `workflow/planning/review.md` |

동일한 Planning 작업에서 이미 읽은 문서를 불필요하게 반복해서 읽지 않는다.

다만 이전 단계로 돌아갔고 관련 규칙을 다시 확인할 필요가 있다면 필요한 범위에서 재확인한다.

---

## Workflow 작업과 상태

Planning은 상위 Workflow에서 생성된 다음 작업 공간을 사용한다.

```text
.workflow/tasks/<task-id>/
├─ status.md
├─ spec.md
├─ design.md
└─ review.md
```

`task-id`와 Workflow 재개 여부는 상위 `workflow.md`의 규칙을 따른다.

`status.md`는 Planning의 상세 기록이 아니라 현재 상태와 이후 단계가 의존하는 핵심 정보를 유지하는 checkpoint다.

Planning 중 다음과 같은 변화가 있으면 필요한 범위에서 `status.md`를 갱신한다.

- 현재 하위 단계 변경
- 사용자 확인 또는 승인 상태 변경
- 이후 단계가 의존하는 핵심 Project Context 확정
- 중요한 Open Question 발생 또는 해결
- Reviewer 선택
- Review round 변경
- Waived finding 발생

---

## 상태 전이

정상적인 Planning 흐름은 다음과 같다.

```text
Discovery
   ↓
Specification
   ↓
Specification Confirmation
   ↓
Design
   ↓
Design Review
   ↓
Design Refinement
   ↓
Approval
```

필요하면 이전 단계로 돌아갈 수 있다.

### Requirement 문제가 발견된 경우

```text
Design Review
   ↓
Specification
```

Review 과정에서 Requirement 자체의 문제가 발견된 경우 Specification으로 돌아간다.

Specification의 의미가 변경되면 `specification.md`의 재확인 규칙을 따른다.

### 사용자 의도의 재확인이 필요한 경우

```text
Design Refinement
   ↓
Specification Confirmation
```

### 기존 전제가 무너진 경우

```text
Design Refinement
   ↓
Discovery
```

새로운 Project Context가 발견되어 기존 전제가 무너졌다면 Discovery로 돌아간다.

단계 전이는 실제 작업 상태를 표현해야 하며,
형식적으로 다음 단계로 넘어가기 위해 사용하지 않는다.

---

## 사용자 개입

Planning 내부에는 서로 다른 성격의 사용자 개입이 존재할 수 있다.

### Specification Confirmation

사용자가 무엇을 필요로 하는지 올바르게 이해했는지 확인한다.

### Design 관련 판단

기술 분석만으로 결정할 수 없고 사용자 의도나 우선순위가 필요한 경우에만 요청한다.

### Approval

확정된 Specification을 현재 Design으로 구현할 것인지 최종 승인받는다.

Specification Confirmation이나 중간 판단을 최종 Approval로 확대 해석하지 않는다.

승인과 확인 표현은 현재 하위 단계와 대화 문맥을 기준으로 해석한다.

이전에 승인된 `spec.md`와 `design.md`가 그대로 유효하다면 기존 Approval을 재사용할 수 있다.

구현 기준의 의미가 변경된 경우에만 다시 Approval을 받는다.

---

## Planning 종료 조건

다음을 만족하면 Planning을 종료하고 Development로 이동한다.

- 사용자가 Specification을 확인했다.
- Specification을 바탕으로 핵심 기술 설계가 작성되었다.
- Design Review가 완료되었다.
- Open 상태의 Blocking finding이 없다.
- Important finding의 처리 상태가 결정되었다.
- Specification과 Design이 서로 일치한다.
- 남아 있는 Waived finding과 주요 risk가 기록되어 있다.
- 사용자가 최종 Design을 승인했다.

승인 시점의 `spec.md`와 `design.md`가 Development의 구현 기준이 된다.
