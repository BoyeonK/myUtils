# workflow/development.md

# Development

Development는 Planning에서 승인된 Specification과 Design을 실제 구현으로 옮기는 단계다.

Development의 목적은 새로운 설계를 만드는 것이 아니라 **승인된 설계를 정확하게 구현하는 것**이다.

---

## 1. 진입 조건

Development에 진입하려면 다음 조건이 충족되어야 한다.

* Planning이 완료되었다.
* `spec.md`가 존재한다.
* `design.md`가 존재한다.
* 독립 설계 검토가 완료되었다.
* 사용자가 구현을 승인했다.

Development에 진입하면 다음과 같이 상태를 표시한다.

```text
[Workflow: Development]
```

---

## 2. 구현 기준

구현의 기준은 다음 우선순위를 따른다.

1. 승인된 Specification
2. 승인된 Design
3. Project Context의 기존 계약과 규칙
4. 구현 과정에서 필요한 세부 판단

구현 편의를 위해 Specification이나 Design의 의미를 임의로 변경하지 않는다.

---

## 3. 구현 전 확인

실제 수정 전에 현재 코드가 Planning 과정에서 확인한 상태와 여전히 일치하는지 필요한 범위에서 확인한다.

Planning 이후 코드나 환경이 변경되어 전제가 무너졌다면 그대로 구현하지 않는다.

필요하면 Planning으로 돌아간다.

---

## 4. Implementation Plan

작업 규모가 충분히 크다면 구현 전에 간단한 실행 순서를 정리한다.

예:

```text
1. public interface 추가
2. 내부 상태 구현
3. lifecycle 구현
4. 기존 시스템과 연결
5. 테스트 추가
```

Implementation Plan은 새로운 설계 문서가 아니다.

이미 승인된 Design을 어떤 순서로 코드에 반영할지 정하는 실행 계획이다.

작은 변경에는 별도의 문서가 필요하지 않을 수 있다.

---

## 5. 구현 규칙

Development 중에는 다음 규칙을 따른다.

### 승인된 범위 유지

현재 작업과 직접 관련되지 않은 리팩터링이나 기능 추가를 함께 수행하지 않는다.

### 기존 스타일 유지

프로젝트가 정의한 기존 코딩 규칙, 파일 구조, 에러 처리 방식 등을 따른다.

### 최소한의 변경

요구사항을 충족하는 범위에서 불필요한 변경을 피한다.

### 검증 가능한 구현

가능하면 구현과 함께 해당 동작을 검증할 수 있는 테스트 또는 검증 방법을 마련한다.

### 임시 우회 방지

테스트를 통과시키기 위해 요구사항이나 기존 계약을 우회하지 않는다.

---

## 6. 구현 중 새로운 문제 발견

구현 과정에서 발견한 문제는 다음과 같이 구분한다.

### 구현상의 세부 문제

승인된 Design의 의미를 바꾸지 않고 해결할 수 있다면 Development 내에서 처리한다.

### Design 변경 필요

API, ownership, 상태 전이, 주요 알고리즘 등 승인된 Design의 의미가 변경되어야 한다면
Development를 중단하고 Planning으로 돌아간다.

```text
Development
   ↓
Planning / Design 또는 Refinement
```

### Requirement 변경 필요

Specification 자체가 잘못되었거나 부족하다면 Planning의 Specification 단계로 돌아간다.

### 별도의 문제

현재 작업의 correctness와 무관한 독립적인 문제는 현재 범위에 자동 포함하지 않는다.

후속 작업 후보로 분리한다.

---

## 7. 개발 중 검증

Development 중에도 필요하면 다음을 실행할 수 있다.

* 부분 빌드
* 관련 단위 테스트
* 정적 검사
* 작은 재현 프로그램
* 개발용 검증

이는 최종 Testing 단계를 대체하지 않는다.

목적은 문제를 가능한 한 일찍 발견하는 것이다.

---

## 8. 종료 조건

다음을 만족하면 Development를 종료한다.

* 승인된 요구사항에 필요한 구현이 존재한다.
* 승인된 Design의 주요 요소가 구현되었다.
* 명백한 미완성 코드가 남아 있지 않다.
* Testing에서 검증 가능한 상태다.
* 구현 과정에서 발견된 설계 변경 요구가 해결되어 있다.

그 후:

```text
Development
   ↓
Testing
```

으로 이동하고 `workflow/testing.md`를 읽는다.
