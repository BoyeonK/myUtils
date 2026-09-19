# workflow/testing.md

# Testing

Testing은 구현이 승인된 Specification과 Design을 만족하고,
기존 시스템의 동작을 의도하지 않게 깨뜨리지 않았는지 검증하는 단계다.

Testing은 단순히 기존 테스트 명령을 한 번 실행하는 것을 의미하지 않는다.

---

## 1. 진입

Testing에 진입하면 다음과 같이 상태를 표시한다.

```text
[Workflow: Testing]
```

검증 기준은 다음이다.

* `spec.md`
* `design.md`
* 프로젝트의 기존 계약
* 기존 테스트
* 변경된 코드

---

## 2. Test Planning

먼저 무엇을 검증해야 하는지 정리한다.

최소한 다음을 고려한다.

### Requirement Validation

Specification의 요구사항이 실제로 충족되는가.

### Acceptance Criteria

Planning에서 정의한 완료 조건을 검증할 수 있는가.

### Regression

기존 기능이 깨지지 않았는가.

### Boundary / Edge Cases

정상 경로 외의 중요한 경계 조건이 있는가.

### Failure Path

오류, 예외, 종료, 자원 부족 등의 상황이 정의된 의미대로 동작하는가.

### Concurrency

동시성이 관련된 작업이라면 의미 있는 경쟁 상태와 경계 조건이 검증되는가.

관련 없는 항목은 생략한다.

---

## 3. 테스트 생성

필요한 테스트가 존재하지 않는다면 Testing 단계에서 테스트를 추가할 수 있다.

테스트는 구현 세부사항을 단순히 그대로 재현하기보다,
가능하면 Specification에서 정의한 외부 계약과 invariant를 검증한다.

테스트를 통과시키기 위해 구현 요구사항을 약화하지 않는다.

---

## 4. 테스트 실행

프로젝트에서 제공하는 기존 테스트 방법을 우선 사용한다.

필요에 따라 다음을 수행할 수 있다.

* unit test
* integration test
* regression test
* build verification
* static analysis
* sanitizer
* stress test
* benchmark
* 수동 검증

Workflow는 특정 도구를 강제하지 않는다.

어떤 테스트를 실행해야 하는지는 변경 내용과 Project Context에 따라 결정한다.

---

## 5. 실패 처리

### 구현 오류

구현이 승인된 Specification 또는 Design과 다르면 Development로 돌아간다.

```text
Testing
   ↓
Development
```

수정 후 다시 Testing을 수행한다.

### 설계 오류

구현이 Design대로 되어 있지만 Design 자체가 요구사항을 만족시키지 못한다면 Planning으로 돌아간다.

```text
Testing
   ↓
Planning
```

### 요구사항 문제

Acceptance Criteria가 모순되거나 실제 사용 요구를 잘못 표현한 것이 확인되면
Planning의 Specification 단계로 돌아간다.

---

## 6. 실행할 수 없는 테스트

필요한 검증을 환경 등의 이유로 실행할 수 없는 경우 이를 PASS로 간주하지 않는다.

다음을 기록한다.

* 실행하지 못한 검증
* 실행하지 못한 이유
* 대신 수행한 검증
* 검증되지 않은 위험

---

## 7. Test Result

Testing 결과는 기본적으로 다음 위치에 기록한다.

```text
.workflow/tasks/<task-id>/test-result.md
```

권장 구조:

```markdown
# Test Result

## Tested

## Passed

## Failed

## Not Tested

## Remaining Risks
```

필요한 경우 더 자세한 결과를 추가할 수 있다.

---

## 8. 종료 조건

다음을 만족하면 Testing을 종료할 수 있다.

* Specification의 Acceptance Criteria를 검증했다.
* 필요한 새 테스트가 추가되었다.
* 관련 기존 테스트가 통과했다.
* 알려진 실패가 남아 있지 않다.
* 실행하지 못한 검증이 있다면 명시되어 있다.
* 남아 있는 위험이 명확하다.

그 후:

```text
Testing
   ↓
Recording
```

으로 이동하고 `workflow/recording.md`를 읽는다.

---

# workflow/recording.md

# Recording

Recording은 작업을 수행하면서 확정된 지식을 프로젝트의 기존 정보 체계에 반영하는 단계다.

Workflow의 작업 문서를 프로젝트의 정식 문서로 그대로 복사하는 단계가 아니다.

---

## 1. 진입

Recording에 진입하면 다음과 같이 표시한다.

```text
[Workflow: Recording]
```

---

## 2. 목적

Planning과 Development 과정에서 다음과 같은 새로운 사실이 생길 수 있다.

* 새로운 공개 사용법
* 새로운 실행 계약
* 아키텍처 변화
* 중요한 설계 결정
* 새로운 제약
* 변경된 동작
* 후속 작업
* 사용자가 알아야 하는 변경사항

Recording에서는 이러한 정보 중 **프로젝트가 이후에도 알아야 하는 내용**을 선별한다.

---

## 3. Project-owned Documentation

기록 위치와 형식은 Workflow가 결정하지 않는다.

현재 프로젝트의 instructions와 기존 문서 체계를 확인하여 적절한 위치를 선택한다.

Workflow는 다음과 같은 개념만 전달한다.

```text
확정된 설계
결정 근거
공개 사용법
운영 규칙
알려진 제약
후속 작업
완료 이력
```

실제 프로젝트가 이를 어디에 저장하는지는 프로젝트가 결정한다.

---

## 4. Workflow Artifact와 Project Artifact의 차이

Workflow artifact는 작업 과정의 기록이다.

예:

```text
.workflow/tasks/<task-id>/spec.md
.workflow/tasks/<task-id>/design.md
.workflow/tasks/<task-id>/review.md
```

Project artifact는 이후 프로젝트 작업에서 기준으로 사용되는 지식이다.

Recording에서는 Workflow artifact 전체를 그대로 Project artifact로 복사하지 않는다.

확정된 정보만 추출하여 프로젝트의 기존 문서 구조에 맞게 반영한다.

---

## 5. 중복 금지

같은 정보를 여러 문서에 반복해서 적지 않는다.

이미 프로젝트의 적절한 위치에 정보가 존재한다면 새로운 문서를 만들지 않고 필요한 부분만 갱신한다.

Workflow 때문에 새로운 프로젝트 문서 체계를 강제로 만들지 않는다.

---

## 6. 기록하지 않아도 되는 것

다음은 일반적으로 프로젝트의 장기 문서에 남길 필요가 없다.

* 구현 과정의 시행착오
* 이미 해결된 일시적인 오류
* 더 이상 의미 없는 설계 후보
* Reviewer가 제안했지만 채택되지 않은 사소한 대안
* 코드 자체에서 명확하게 드러나는 세부 구현

단, 이후 설계 판단에 중요한 근거라면 프로젝트 규칙에 따라 기록할 수 있다.

---

## 7. 후속 작업

현재 작업에서 발견했지만 범위에 포함되지 않은 문제는
프로젝트가 사용하는 기존 작업 관리 방식에 맞게 기록할 수 있다.

현재 작업에 몰래 포함하지 않는다.

---

## 8. 종료 조건

다음을 확인한다.

* 이번 작업으로 프로젝트가 새롭게 알아야 하는 사실을 식별했다.
* 필요한 프로젝트 문서를 갱신했다.
* 불필요한 문서 중복을 만들지 않았다.
* 후속 작업이 필요한 경우 적절히 분리했다.

프로젝트 문서 변경이 필요하지 않다면 그 사실 자체로 Recording을 완료할 수 있다.

그 후:

```text
Recording
   ↓
Completion
```

으로 이동하고 `workflow/completion.md`를 읽는다.

---
