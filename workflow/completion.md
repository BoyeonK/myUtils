# workflow/completion.md

# Completion

Completion은 하나의 Workflow 작업이 실제로 종료 가능한 상태인지 최종 확인하는 단계다.

새로운 기능을 추가하거나 설계를 변경하는 단계가 아니다.

---

## 1. 진입

Completion에 진입하면 다음과 같이 표시한다.

```text
[Workflow: Completion]
```

---

## 2. 최종 검증

다음 항목을 확인한다.

### Scope

* 승인된 작업 범위가 구현되었다.
* 승인되지 않은 범위 확장이 없는지 확인했다.

### Specification

* 주요 Requirements가 구현되었다.
* Acceptance Criteria를 만족한다.

### Design

* 구현이 승인된 Design과 일치한다.
* Design과 다르게 구현된 부분이 있다면 명시적으로 해결되었다.

### Testing

* 필요한 테스트가 수행되었다.
* 알려진 실패가 남아 있지 않다.
* 실행하지 못한 검증과 위험이 명시되어 있다.

### Recording

* 프로젝트에 남아야 하는 정보가 필요한 위치에 반영되었다.
* 별도의 후속 작업이 필요한 문제는 분리되었다.

### Repository State

가능한 범위에서 다음을 확인한다.

* 의도하지 않은 파일 변경
* 임시 debug 코드
* 임시 출력
* 실험용 파일
* 미완성 placeholder
* 현재 작업으로 발생한 불필요한 변경

---

## 3. 완료 불가 조건

다음과 같은 상태라면 완료로 처리하지 않는다.

* Acceptance Criteria가 충족되지 않았다.
* 필요한 테스트가 실패하고 있다.
* 구현이 승인된 요구사항과 다르다.
* 해결되지 않은 Blocking 문제가 있다.
* 중요한 미검증 위험이 숨겨져 있다.

문제의 성격에 따라 적절한 단계로 돌아간다.

---

## 4. Completion Record

최종 결과는 기본적으로 다음 위치에 기록한다.

```text
.workflow/tasks/<task-id>/completion.md
```

권장 구조:

```markdown
# Completion

## Implemented

## Verified

## Project Documentation Updated

## Remaining Risks

## Follow-up Tasks
```

내용이 없는 항목은 생략할 수 있다.

---

## 5. 사용자 보고

최종 응답은 다음과 같이 시작한다.

```text
[Workflow: Completion]
```

그리고 최소한 다음을 전달한다.

```text
구현한 내용

검증한 내용

프로젝트에 반영한 기록

남은 위험 또는 미검증 사항

후속 작업
```

단순히 "완료했습니다"라고 보고하지 않는다.

어떤 근거로 완료 상태라고 판단했는지 확인할 수 있어야 한다.

---

## 6. Workflow 종료

Completion의 종료 조건을 만족하면 현재 Workflow 작업을 종료한다.

이 시점 이후 같은 세션에서 발생하는 일반적인 질문이나 추가 요청은
자동으로 현재 Workflow의 연장으로 취급하지 않는다.

새로운 독립 작업은 프로젝트가 정의한 Workflow 진입 방법을 통해 새로 시작한다.
