# ADR-0005 Reserve 단일 진입점과 tail reclaim 조건

- Status: Accepted
- Date: 2026-09-15

## Context

실제 사용 모델은 "최대 크기로 잡고 나중에 실제 크기를 확정"이다.

```cpp
auto buffer = manager.Reserve(1024);
auto view = std::move(buffer).Commit(80);   // 944바이트는?
```

확정 후 남은 꼬리를 회수하지 않으면 그 944바이트는 chunk에서 영구히 사라진다.
이건 단순 낭비가 아니라 **이용률이 그대로 메모리 사용량 배수**가 되는 문제다.

```
미완료 send 10,000건, 실제 패킷 200 B

정확한 크기로 할당       chunk당 80개 → 살아있는 chunk 125개 →  2 MB
1024로 잡고 200 커밋     chunk당 16개 → 살아있는 chunk 625개 → 10 MB
```

5배이고, chunk 크기를 낮춰 얻은 pinning 완화 효과와 **곱해진다.**

## Decision

### 진입점은 `Reserve` 하나

```cpp
[[nodiscard]] SendBuffer Reserve(std::size_t capacity);
SendView SendBuffer::Commit(std::size_t writeSize) &&;
```

크기를 정확히 아는 경우는 `Reserve(N)` / `Commit(N)`이라는 **퇴화 사례**이며 비용이
정확히 0이다. 회수 로직이 `used == reserved`일 때 아무 일도 하지 않기 때문이다.

이름을 `Allocate`가 아니라 `Reserve`로 둔 것은 "아직 최종 크기가 아니다"라는 commit
의무를 이름에 담기 위해서다.

### tail reclaim은 두 조건이 모두 참일 때만

```cpp
if (manager.CurrentChunk().get() == _chunk.get() &&
    _chunk->Offset() == _offset + _capacity)
{
    _chunk->Rewind(_offset + usedSize);
}
```

두 조건은 **서로 다른 것을 증명**하므로 둘 다 필요하다.

| 조건 | 증명하는 것 |
|---|---|
| 내 TLS manager의 Current == 내 chunk | **안전성.** 이 스레드가 owner이므로 `_offset`을 써도 레이스가 없다 |
| `chunk.Offset() == 내시작 + 예약크기` | **정확성.** 내 뒤에 다른 할당이 없으므로 되돌려도 남의 영역을 침범하지 않는다 |

첫 번째가 owner임을 **증명**하는 이유는, chunk가 한 시점에 최대 하나의 manager에게만
Current일 수 있기 때문이다([ADR-0001](0001-refcounted-arena.md) — manager가 참조를
들고 있어 Pool이 다른 worker에게 내줄 수 없다). 그래서 thread id 비교도 atomic도
필요 없다.

두 번째가 빠지면 이렇게 깨진다.

```
A = Reserve(1024)   // [0, 1024)
B = Reserve(100)    // [1024, 1124)
A.Commit(80)        // A는 여전히 Current chunk이지만, 되돌리면 B를 침범한다
```

## 기각된 대안

### `AllocateExact` + `Reserve` 두 API 제공

크기 계산이 전체 순회를 한 번 더 요구한다면 exact를 강제하는 게 CPU 낭비라는 지적은
맞다. 하지만 그 결론은 **Reserve/Commit 모델을 지원해야 한다**는 것이지 API를 둘로
나눠야 한다는 뜻이 아니다. 두 함수의 구현이 동일하므로 얻는 성능 이득이 없고, 진입점이
둘이면 문서화·테스트·계측·향후 확장이 전부 두 배가 된다.

### 직렬화 계층이 항상 정확한 크기를 계산하도록 규율

근본적이지만 조건부로만 맞다. 크기 계산 자체가 전체 객체를 한 번 더 순회해야 한다면
메모리 몇백 바이트를 아끼려고 CPU를 두 배로 쓰는 셈이다.

## Consequences

- Commit 없이 소멸해도 같은 경로로 롤백된다(`usedSize = 0`). 에러 경로에서 그냥
  return해도 chunk 공간이 새지 않는다.
- LIFO라서 뒤에 다른 할당이 끼면 회수할 수 없다. **이건 정상 동작이다.**
  이동 대입 `a = std::move(b)`에서 `a`의 예약이 회수되지 않는 것도 같은 이유다.
- 큰 chunk를 쓰는 경우일수록 회수의 가치가 커진다. 실측(테스트 스위트 기준)에서
  과다 예약분 7.06 MB 중 **5.99 MB(85%)가 회수**됐다.
- 회수량은 `SnapshotStats().reclaimedBytes`로 관측한다. `committedBytes / reservedBytes`가
  이용률이며, 이 값이 chunk 크기 조정의 1차 근거다.
