# Development

Development는 승인된 Specification과 Design을 실제 코드로 구현하는 단계다.

Planning에서 확정된 기준을 구현하되,
구현 과정에서 안전하게 결정할 수 있는 지역적인 세부사항은 Development에서 판단한다.

Development에서는 승인된 요구사항이나 핵심 설계를 임의로 변경하지 않는다.

---

## 1. 입력

Development는 Planning에서 승인된 다음 정보를 기준으로 시작한다.

```text
.workflow/tasks/<task-id>/
├─ status.md
├─ spec.md
├─ design.md
└─ review.md
```

Development 진입 시 다음을 확인한다.

* 승인된 `spec.md`
* 승인된 `design.md`
* 남아 있는 Waived finding
* 알려진 주요 위험과 Open Question
* 현재 Project Context와 기존 구현 상태

승인 시점의 `spec.md`와 `design.md`가 구현의 기준이다.

---

## 2. 기본 원칙

Development의 목적은 승인된 설계를 코드로 옮기는 것이다.

AI는 구현 과정에서 필요한 기술적 판단을 수행할 수 있다.

다만 다음을 구분한다.

```text
지역적 구현 세부사항
→ Development에서 결정

Requirement 또는 핵심 Design 변경
→ Planning으로 복귀
```

Implementation 과정에서 발견한 문제를 해결하기 위해
승인된 Specification이나 Design의 의미를 조용히 변경하지 않는다.

---

## 3. Specification과 Design의 변경

Development에서는 `spec.md`와 `design.md`를 구현 기준으로 읽는다.

Development 과정에서 이 두 artifact의 내용을 임의로 수정하지 않는다.

구현 중 Specification 또는 핵심 Design을 변경해야 한다고 판단되면
현재 구현을 계속 확장하기보다 Planning으로 돌아간다.

```text
Development
   ↓
Planning
```

Planning에서 필요한 변경과 재확인 또는 재승인을 완료한 뒤
다시 Development로 진입한다.

표현 수정이나 기록 정리가 아니라
구현 기준의 의미가 바뀌는 변경이라면 반드시 Planning을 거친다.

---

## 4. 지역적 구현 세부사항

Planning의 Design은 구현 과정에서 안전하게 결정할 수 있는
지역적인 세부사항을 Development에 남길 수 있다.

예를 들어 다음과 같은 결정은 핵심 설계를 변경하지 않는 범위에서
Development가 직접 판단할 수 있다.

* private helper의 구조
* 지역 변수와 내부 표현
* 함수 내부의 구체적인 제어 흐름
* 이미 정해진 계약을 구현하기 위한 자료구조의 세부 사용 방식
* 코드 배치와 작은 리팩터링
* 명명과 기타 지역적인 구현 선택

구현 세부사항이라는 이유만으로 모든 결정을 Development에서 처리할 수 있는 것은 아니다.

다음 중 하나라도 변경된다면 지역적인 구현 세부사항으로 취급하지 않는다.

* Requirement 또는 Acceptance Criteria의 의미
* 외부에서 관찰 가능한 API 또는 동작
* component 간 책임 경계
* ownership 또는 핵심 lifetime
* correctness를 좌우하는 concurrency invariant
* synchronization contract
* 중요한 failure semantics
* 주요 resource lifecycle
* 승인된 Design의 핵심 trade-off
* 기존 Project Context의 중요한 제약

이러한 변경이 필요하면 Planning으로 돌아간다.

판단이 불분명한 경우에는
구현 편의를 이유로 지역적 세부사항으로 확대 해석하지 않는다.

---

## 5. Waived Finding

Planning에서 `Waived` 상태로 남은 finding은
위험이 사라졌다는 의미가 아니다.

Development는 해당 finding과 남아 있는 위험을 인지한 상태에서 구현을 진행한다.

Waived finding은 다음을 허용하지 않는다.

* Requirement 위반
* Acceptance Criteria 무시
* 실제 오류를 정상 동작으로 간주
* Testing 실패를 성공으로 취급
* 승인된 Design과 다른 구현을 정당화

### 구현 중 위험이 실제로 드러난 경우

Waived finding과 관련된 위험이 구현 중 실제 문제로 드러났다고 해서
항상 Planning으로 돌아가는 것은 아니다.

현재 승인된 Design 안에서 지역적으로 처리할 수 있고
Requirement와 Acceptance Criteria를 그대로 만족할 수 있다면
Development에서 처리할 수 있다.

반면 다음과 같은 경우에는 Planning으로 돌아간다.

* Waive 당시의 전제가 잘못된 것으로 확인된 경우
* 위험의 실제 영향이 승인 당시 이해한 범위를 넘어서는 경우
* Requirement 또는 Acceptance Criteria를 만족할 수 없는 경우
* 해결하려면 핵심 Design을 변경해야 하는 경우
* 새로운 사용자 판단이 필요한 trade-off가 발생한 경우

Waived finding이 구현 과정에서 실제로 제거된 것으로 보이더라도
Development에서 검증 완료로 확정하지 않는다.

필요한 경우 `status.md`에 해당 사실을 남기고,
실제 해결 여부는 Testing에서 검증한다.

---

## 6. Acceptance Criteria의 사용

Acceptance Criteria는 Development와 Testing에서 서로 다른 목적으로 사용한다.

### Development

Development에서는 각 Acceptance Criteria를 만족시키기 위한 구현이 존재하는지 확인한다.

즉,

> 검증할 대상이 구현되어 있는가

를 판단하는 기준으로 사용한다.

### Testing

Testing에서는 실제 실행과 검증을 통해

> Acceptance Criteria가 실제로 만족되는가

를 확인한다.

따라서 Development에서 코드가 구현되었다는 사실만으로
Acceptance Criteria가 충족되었다고 확정하지 않는다.

최종 검증 책임은 Testing에 있다.

---

## 7. 테스트 코드와 개발 중 확인

Development 과정에서 구현에 필요한 테스트 코드를 작성하거나 수정할 수 있다.

또한 명백한 구현 오류를 빠르게 발견하기 위해
필요한 범위에서 build, compile, focused test 또는 기타 확인을 수행할 수 있다.

그러나 이러한 확인은 Testing 단계의 공식 검증을 대신하지 않는다.

Development에서 수행한 확인 결과만을 근거로
전체 구현이 검증되었다고 판단하지 않는다.

---

## 8. Project Context

Development는 Planning에서 확인된 Project Context를 재사용한다.

이미 확인된 자료를 구현 과정에서 불필요하게 반복해서 조사하지 않는다.

다만 다음과 같은 경우에는 필요한 범위에서 다시 확인할 수 있다.

* 실제 코드가 Planning에서 확인한 상태와 다른 경우
* 관련 코드가 변경된 경우
* 구현 과정에서 기존 전제가 의심되는 경우
* 새롭게 발견된 구조가 현재 Design에 영향을 주는 경우

새로운 Project Context가 승인된 Design의 전제를 무너뜨린다면
Development에서 임의로 설계를 수정하지 않고 Planning으로 돌아간다.

---

## 9. `status.md`

Development의 실제 코드 상태는 가능한 한 repository와 version control을 source of truth로 사용한다.

따라서 `status.md`에 변경된 파일 목록이나 구현 내용을 상세하게 복제하지 않는다.

다만 세션이 끊긴 뒤 작업을 안전하게 이어가기 위해 필요한
Workflow 수준의 상태는 기록할 수 있다.

예:

* 현재 단계와 하위 작업 상태
* 현재 구현 중인 component 또는 작업 영역
* 구현을 막고 있는 blocker
* 아직 처리되지 않은 중요한 구현 항목
* Planning 복귀 여부를 판단 중인 문제
* 관련 Waived finding의 현재 상황
* 이후 세션에서 반드시 알아야 하는 임시 전제

코드나 Git 상태만 확인하면 알 수 있는 정보는 가능한 한 중복 기록하지 않는다.

Development 상태가 의미 있게 변경되거나
세션 재개에 필요한 정보가 생긴 경우 Main AI는 `status.md`를 갱신한다.

---

## 10. 구현 중 문제 발견

구현 과정에서 문제가 발견되면 문제의 성격에 따라 처리한다.

### 지역적 구현 문제

승인된 Specification과 Design을 변경하지 않고 해결할 수 있다.

```text
Development
   ↓
Development
```

현재 단계에서 해결한다.

### 핵심 Design 문제

승인된 설계를 변경해야 한다.

```text
Development
   ↓
Planning
```

Planning으로 돌아간다.

### Requirement 문제

Requirement 자체가 모순되거나 구현할 수 없거나
사용자 의도의 재확인이 필요하다.

```text
Development
   ↓
Planning / Specification
```

필요한 요구사항 확인부터 다시 수행한다.

별개의 문제를 발견했다는 이유만으로 현재 작업의 범위를 자동으로 확장하지 않는다.

---

## 11. 사용자 판단

구현 과정에서 단순한 기술 선택을 사용자에게 불필요하게 떠넘기지 않는다.

다만 다음과 같은 경우에는 Planning으로 돌아가
필요한 사용자 판단을 받는다.

* 사용자 의도에 따라 결과가 달라지는 경우
* Requirement 의미를 결정해야 하는 경우
* 승인된 핵심 trade-off를 바꿔야 하는 경우
* 새로운 위험을 명시적으로 수용해야 하는 경우

Development 안에서 새로운 Requirement나 핵심 Design을 사용자와 즉석에서 확정하지 않는다.

---

## 12. 종료 조건

다음을 만족하면 Development를 종료하고 Testing으로 이동한다.

* 승인된 Specification의 구현 대상이 코드에 반영되어 있다.
* 승인된 Design의 핵심 결정이 구현에 반영되어 있다.
* 현재 작업에 필요한 Acceptance Criteria를 검증할 대상이 구현되어 있다.
* 알려진 구현 blocker가 남아 있지 않다.
* Planning으로 돌아가야 할 미해결 Requirement 또는 핵심 Design 문제가 없다.
* Development에서 발견된 중요한 상태가 필요한 범위에서 `status.md`에 반영되어 있다.
* 구현 결과가 Testing에서 검증 가능한 상태다.

Development 종료는 구현이 **검증되었다는 의미가 아니다.**

이는 구현이 완료되어 다음 단계에서 검증할 준비가 되었다는 의미다.

조건을 만족하면 다음 상태로 이동한다.

```text
Development
   ↓
Testing
```

Main AI는 `status.md`의 현재 상태를 Testing으로 갱신한다.
