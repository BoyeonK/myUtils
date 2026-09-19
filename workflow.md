# AI Development Workflow

이 문서는 AI를 활용한 개발 작업의 공통 절차와 상태 전이 규칙을 정의한다.

---

## 1. 목적

Workflow의 목적은 AI에게 단순히 구현을 맡기는 것이 아니라 다음 과정을 명시적으로 분리하여 수행하는 것이다.

- 무엇을 만들 것인지 구체화한다.
- 현재 프로젝트의 문맥과 제약을 확인한다.
- 가능한 설계를 탐색한다.
- 독립적인 관점에서 설계를 검토한다.
- 승인된 설계를 구현한다.
- 구현 결과를 검증한다.
- 확정된 지식을 프로젝트에 반영한다.
- 작업이 실제로 완료되었는지 확인한다.

기본 Workflow는 다음 다섯 단계로 구성된다.

```text
Planning
   ↓
Development
   ↓
Testing
   ↓
Recording
   ↓
Completion
```

각 단계의 세부 규칙은 별도의 문서가 담당한다.

| 단계 | 문서 |
|---|---|
| Planning | `workflow/planning.md` |
| Development | `workflow/development.md` |
| Testing | `workflow/testing.md` |
| Recording | `workflow/recording.md` |
| Completion | `workflow/completion.md` |

각 단계에 진입하면 다른 작업을 수행하기 전에 해당 단계 문서를 읽는다.

---

## 2. Project Context

Workflow 수행 중에는 현재 작업과 관련된 Project Context를 필요한 범위에서 확인한다.

Project Context에는 필요에 따라 다음이 포함될 수 있다.

- project instructions와 repository 구조
- 관련 코드와 테스트
- 설계·아키텍처·의사결정 기록
- 빌드·실행·검증 방법
- coding/documentation convention
- 현재 상태, 미결정 사항 및 작업 이력

구체적인 파일명이나 저장 위치는 프로젝트마다 다를 수 있다.

Workflow는 현재 repository를 조사하여 관련 구조와 자료를 발견하고,
발견한 프로젝트 고유 문맥을 현재 작업에 활용한다.

프로젝트 고유 구조가 현재 작업에서 확인되었다고 해서
다른 프로젝트에도 같은 구조가 존재한다고 가정하지 않는다.

앞선 단계에서 확인한 Project Context는 이후 단계에서도 유효한 작업 문맥으로 재사용한다.

단, 다음과 같은 경우에는 필요한 범위에서 다시 확인한다.

- repository 상태가 변경되었을 가능성이 있는 경우
- 코드나 문서가 수정된 경우
- 기존 전제의 유효성이 의심되는 경우
- 새로운 정보가 기존 판단과 충돌하는 경우

---

## 3. Workflow 작업 공간

Workflow 자체가 사용하는 작업용 산출물은 프로젝트의 정식 문서와 구분한다.

기본 작업 공간은 다음 구조를 사용한다.

```text
.workflow/
└─ tasks/
   └─ <task-id>/
      ├─ status.md
      ├─ spec.md
      ├─ design.md
      ├─ review.md
      ├─ test-result.md
      └─ completion.md
```

`.workflow/`는 Workflow가 소유하는 기본 작업 공간이다.

별도의 명확한 이유가 없다면 프로젝트마다 위치를 변경하지 않고 위 구조를 사용한다.

### task-id

각 Workflow 작업은 하나의 `task-id`로 식별한다.

새 Workflow 작업을 생성할 때 Main AI가 작업 내용을 나타내는 간결한 `task-id`를 정한다.

기본적으로 사람이 읽을 수 있는 kebab-case 이름을 사용한다.

예:

```text
thread-pool
actor-shutdown
inventory-save
```

사용자나 프로젝트에서 명시적인 task-id를 제공한 경우에는 가능한 한 이를 사용한다.

동일한 task-id가 이미 존재하는 경우에는 먼저 기존 작업의 재개인지 확인한다.

동일 작업의 재개라면 기존 task를 사용하고,
별개의 작업이라면 기존 작업과 충돌하지 않는 새로운 task-id를 사용한다.

### status.md

`status.md`는 현재 Workflow 상태와
세션을 넘어 작업을 이어가기 위해 필요한 최소 정보를 기록하는 checkpoint다.

재개 시 상태를 안정적으로 복원할 수 있도록 최소한 다음 필드는 고정해서 사용한다.

```text
Task: <task-id>
Stage: <현재 단계 / 하위 단계>
Approval: none | spec-confirmed | design-approved
Reviewer: <선택된 Reviewer 또는 none>
Review Round: <현재 round 또는 0>
```

필요에 따라 다음 정보를 추가로 기록할 수 있다.

- 주요 Open Question
- 관련 Workflow artifact
- 현재 작업에서 확인한 핵심 Project Context
- 현재 작업에서 확정된 주요 결정
- Waived finding과 알려진 risk

상세한 작업 로그나 Project Context 전체를 복사하지 않는다.

현재 상태를 복원하고 다음 작업을 이어가기 위해 필요한 정보만 기록한다.

Workflow 상태가 변경되거나
이후 단계가 의존하는 중요한 정보가 확정되면
Main AI는 `status.md`를 필요한 범위에서 갱신한다.

### spec.md

무엇을 만들어야 하는지를 정의한다.

### design.md

어떻게 구현할 것인지에 대한 현재 승인 후보 설계를 정의한다.

### review.md

설계 검토 결과와 finding의 상태를 기록한다.

### test-result.md

수행한 검증, 결과, 미검증 항목과 남은 위험을 기록한다.

### completion.md

최종 작업 결과와 완료 판정을 기록한다.

모든 파일이 항상 필요한 것은 아니다.

작업의 성격과 현재 Workflow 상태에 따라 필요한 artifact만 사용할 수 있다.

---

## 4. 정보 상태

Workflow에서는 중요한 정보를 가능한 한 다음 세 상태로 구분한다.

### Confirmed

사용자, 코드, 테스트, 프로젝트 문서 또는 기타 검증 가능한 자료를 통해 확인된 사실.

### Proposed

AI가 현재 요구사항과 Project Context를 바탕으로 제안한 내용.

아직 확정된 요구사항이나 설계로 취급하지 않는다.

### Open

아직 결정되지 않았거나 추가 정보 또는 판단이 필요한 사항.

AI는 Proposed 또는 Open 상태의 내용을 Confirmed인 것처럼 취급하지 않는다.

---

## 5. 공통 규칙

Workflow가 활성화된 동안 다음 규칙을 따른다.

1. 현재 상태를 응답 시작 부분에 표시한다.

```text
[Workflow: Planning]
```

필요하면 하위 상태도 표시한다.

```text
[Workflow: Planning / Discovery]
```

2. 현재 단계에서 허용되지 않은 작업을 앞당겨 수행하지 않는다.

3. 확인되지 않은 요구사항을 임의로 확정하지 않는다.

4. AI의 제안을 사용자 요구사항이나 기존 프로젝트 결정과 구분한다.

5. 프로젝트 고유 구조가 발견되면 현재 작업에서 활용한다.

6. 앞선 단계에서 확인한 Project Context를 이후 단계에서도 재사용하되, 필요하면 다시 검증한다.

7. 별개의 문제를 발견했다는 이유만으로 현재 작업의 범위를 자동으로 확장하지 않는다.

8. 기존 전제가 잘못되었다면 적절한 이전 단계로 돌아간다.

9. 필요한 검증을 수행하지 못했다면 성공한 것으로 간주하지 않는다.

10. Workflow 상태는 실제 작업 상태를 표현해야 하며, 형식적으로 다음 단계로 넘어가기 위해 사용하지 않는다.

11. Workflow 상태가 변경되거나 이후 단계가 의존하는 중요한 정보가 확정되면 `status.md`를 갱신한다.

---

## 6. Workflow 진입과 재개

Workflow 진입 시 Main AI는 먼저 `.workflow/tasks/`의 기존 작업을 확인한다.

현재 요청이 기존 작업의 연속임을 나타내거나,
동일하거나 유사한 task가 존재하는 경우 해당 task의 `status.md`를 확인한다.

현재 요청과 대응하는 기존 작업이 명확하면 해당 task를 재개한다.

대응하는 작업이 여러 개이거나 판단이 불분명하면 임의로 선택하지 않고 사용자에게 확인한다.

대응하는 기존 작업이 없다면 새로운 Workflow 작업으로 시작한다.

### 새로운 작업

새로운 작업이라면 Main AI는 `task-id`를 정하고 다음 작업 공간을 생성한다.

```text
.workflow/tasks/<task-id>/
```

새 작업의 최초 상태는 항상 다음과 같다.

```text
[Workflow: Planning / Discovery]
```

Planning에 진입하면 `workflow/planning.md`를 읽는다.

이후 각 단계로 이동할 때 해당 단계 문서를 읽는다.

### 기존 작업 재개

현재 요청이 기존 Workflow 작업의 연속이고
해당 task의 유효한 `status.md`가 존재한다면
기록된 Workflow 상태에서 작업을 재개한다.

재개할 때에는 `status.md`와 관련 Workflow artifact를 기준으로
현재 상태와 필요한 Project Context를 복원한다.

기존 Project Context를 다시 검증해야 하는지는
이 문서의 `Project Context` 규칙을 따른다.

`status.md`가 없거나 현재 상태를 신뢰할 수 없는 경우에는
기존 상태를 추측하여 이어가지 않는다.

필요한 범위에서 상태를 복원하거나 적절한 이전 단계로 돌아간다.

### 완료된 task

`Completion` 상태의 task는 일반적인 재개 대상으로 취급하지 않는다.

완료된 작업에 대한 추가 요청은
이 문서의 Workflow 종료 규칙에 따라
기존 task의 후속 작업인지 새로운 Workflow 작업인지 판단한다.

---

## 7. 사용자 확인과 승인

기본적으로 `Planning -> Development` 전이에는 사용자의 명시적인 승인이 필요하다.

Planning 내부에서도 요구사항이나 사용자 의도를 확정하기 위한
별도의 확인 지점이 존재할 수 있다.

이러한 내부 확인의 세부 규칙은 `workflow/planning.md`와
해당 하위 단계 문서를 따른다.

Planning 내부의 확인은 `Planning -> Development` 승인과 동일한 의미로 취급하지 않는다.

승인 또는 확인 표현은 현재 Workflow 단계와 대화 문맥을 기준으로 해석한다.

예를 들어 Specification Confirmation 단계에서 사용자가

```text
승인
```

이라고 응답했다면 이는 현재 Specification에 대한 확인으로 해석하며,
최종 Design 승인이나 Development 진입 승인으로 자동 확대 해석하지 않는다.

사용자의 의도가 불분명한 경우에는 더 넓은 승인으로 추정하지 않는다.

최종 Approval 전에는 구현을 시작하지 않는다.

Development 이후에는 별도의 승인 조건이 없는 한 Workflow가 다음 단계로 계속 진행할 수 있다.

단, 승인된 요구사항이나 설계를 변경해야 한다면 Planning으로 돌아간다.

---

## 8. 상태 전이

정상:

```text
Planning -> Development -> Testing -> Recording -> Completion
```

Development 중 설계 변경 필요:

```text
Development -> Planning
```

Testing 중 구현 오류:

```text
Testing -> Development
```

Testing 중 요구사항 또는 설계 문제:

```text
Testing -> Planning
```

Recording 또는 Completion에서도 이전 판단의 오류가 발견되면 적절한 단계로 돌아갈 수 있다.

이전 단계로 돌아간 경우 필요한 수정과 재검증을 완료한 뒤 다시 정상 흐름으로 진행한다.

세부 단계 안에서의 전이와 재확인 규칙은 해당 단계 문서가 정의한다.

---

## 9. 단계별 책임

### Planning

무엇을 만들 것인지와 어떤 설계로 구현할 것인지 확정한다.

→ `workflow/planning.md`

### Development

승인된 Specification과 Design을 구현한다.

→ `workflow/development.md`

### Testing

구현이 요구사항과 기존 시스템의 계약을 만족하는지 검증한다.

→ `workflow/testing.md`

### Recording

작업을 통해 확정된 지식을 현재 프로젝트의 기존 정보 체계에 반영한다.

→ `workflow/recording.md`

### Completion

전체 작업이 완료 조건을 만족하는지 최종 확인한다.

→ `workflow/completion.md`

---

## 10. Workflow 종료

Completion 조건을 만족하면 현재 작업의 Workflow는 종료된다.

완료 상태는 `status.md`와 `completion.md`에 반영한다.

같은 세션에서 이후 발생하는 요청은 자동으로 새로운 Workflow 작업으로 취급하지 않는다.

새로운 독립 작업을 Workflow로 수행하려면
프로젝트가 정의한 Workflow 진입 방법을 다시 사용한다.

완료된 작업을 다시 수정해야 하는 경우에는
기존 task의 후속 작업인지 새로운 Workflow 작업인지 현재 요청의 성격에 따라 판단한다.
