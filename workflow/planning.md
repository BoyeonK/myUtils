# Planning

Planning은 사용자의 초기 요청을 **구현 가능한 요구사항과 검토된 설계로 발전시키는 단계**다.

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

Specification에서는 **사용자가 무엇을 필요로 하는지**를 명확하게 만든다.

Design에서는 확정된 Specification을 바탕으로 **어떻게 구현할 것인지**를 결정한다.

이 둘의 의사결정 주체와 목적을 혼동하지 않는다.

---

## 하위 단계 문서

각 하위 단계에 진입하면 해당 작업을 수행하기 전에 관련 문서를 읽는다.

| 하위 단계                      | 문서                                   |
| -------------------------- | ------------------------------------ |
| Discovery                  | `workflow/planning/discovery.md`     |
| Specification              | `workflow/planning/specification.md` |
| Specification Confirmation | `workflow/planning/specification.md` |
| Design                     | `workflow/planning/design.md`        |
| Design Review              | `workflow/planning/review.md`        |
| Design Refinement          | `workflow/planning/review.md`        |
| Approval                   | `workflow/planning/review.md`        |

한 단계 안에서 이미 읽은 문서를 동일한 Planning 작업 중 반복해서 읽을 필요는 없다.

단, 이전 단계로 되돌아갔고 관련 문서의 규칙을 다시 확인할 필요가 있다면 재확인할 수 있다.

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

예:

```text
Design Review
   ↓
Specification
```

Review 과정에서 Requirement 자체의 문제가 발견된 경우.

```text
Design Refinement
   ↓
Specification Confirmation
```

사용자의 의도를 다시 확인해야 하는 경우.

```text
Design Refinement
   ↓
Discovery
```

새로운 Project Context가 발견되어 기존 전제가 무너진 경우.

단계 전이는 실제 작업 상태를 표현해야 하며,
형식적으로 다음 단계로 넘어가기 위해 사용하지 않는다.

---

## Planning Artifact

Planning 과정에서는 기본적으로 다음 Workflow artifact를 사용한다.

```text
.workflow/tasks/<task-id>/
├─ spec.md
├─ design.md
└─ review.md
```

각 artifact의 상세 작성 규칙은 해당 하위 단계 문서에서 정의한다.

---

## Planning 종료

Planning이 완료되었을 때 기본적으로 다음 artifact가 존재한다.

```text
spec.md
design.md
review.md
```

그리고 다음 조건을 만족해야 한다.

* 사용자가 Specification을 확인했다.
* Specification을 바탕으로 기술 설계가 작성되었다.
* Design Review가 완료되었다.
* Blocking Review issue가 해결되었다.
* Specification과 Design이 서로 일치한다.
* 사용자가 최종 Design을 승인했다.

모든 조건을 만족하면 Planning을 종료하고 Development로 이동한다.
