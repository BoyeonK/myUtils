# ADR-0001 SendBuffer를 refcounted arena + bump allocation으로

- Status: Accepted
- Date: 2026-09-15

## Context

기존 `Network.h`/`Network.cpp`의 SendBuffer는 스레드 로컬 6000바이트 chunk를 잘라
쓰는 구조였는데, 여러 결함이 있었다.

- `SendBuffer::Close()`를 한 번만 빠뜨리면 chunk의 `_isOpen`이 영구히 `true`로 남고,
  **다음 `Open`에서 `ASSERT_CRASH`로 프로세스가 죽었다.** `~SendBuffer()`가 비어 있어
  되돌릴 경로가 없었다. 원인 지점과 크래시 지점이 달라 디버깅이 최악이다.
- 그 상태를 막으려던 `ASSERT_CRASH(IsOpen() == false)` 가드는 chunk가 교체되면
  그냥 통과했다. **같은 실수가 때로는 크래시, 때로는 무증상**이었다.
- 6000바이트를 넘는 요청은 크래시였다.
- `std::array<uint8_t, 6000> _buffer = {}` 때문에 chunk를 꺼낼 때마다 6 KB를 0으로
  채웠다. 쓰기 전 내용은 의미가 없으므로 순수 낭비다.
- 스레드당 동시에 열 수 있는 버퍼가 1개로 제한됐다.

한편 비동기 I/O 환경에서는 `AsyncSend`가 반환한 뒤에도 커널이 완료할 때까지 버퍼가
유효해야 한다. 즉 **잘라낸 조각이 그것을 만든 chunk보다 오래 살 수 있다.**

## Decision

thread-local bump allocator + **chunk 단위 참조 카운트** 구조로 전환한다.

- `SendBufferChunk`가 연속 바이트 영역을 소유하고 커서를 밀며 잘라준다.
- `_offset`은 동기화하지 않는다. 한 시점에 하나의 owner thread만 allocation하며,
  소유권 이전은 반드시 ChunkPool을 경유해 happens-before를 확보한다.
- 잘라낸 조각이 chunk의 `ChunkRef`를 하나씩 들고, 마지막 참조가 사라지면 chunk가
  재사용 가능해진다.
- **`SendBufferManager`가 Current Chunk에 대한 참조를 하나 보유한다.**

마지막 항목이 이 ADR의 핵심이다. 이 참조가 있어야
**"refcount == 0" 이 "아무도 이 chunk를 모른다" 와 정확히 같은 뜻**이 된다.

## 기각된 대안

### outstanding 조각만 참조 카운트에 포함

처음 설계안이 이랬다. 그러면 다음이 발생한다.

```
Worker 0 : Chunk A 획득, Current으로 설정          refcount 0
Worker 0 : Reserve(100) → Buffer1                 refcount 1
Worker 3 : I/O completion → Buffer1 release       refcount 0
           → Chunk A를 Pool에 반환   ←←← 아직 Worker 0이 쓰고 있다
Worker 1 : Pool에서 Chunk A 획득
Worker 0 : Reserve(200)     ← 두 worker가 같은 chunk에 동시 bump allocation
```

`_offset`을 두 스레드가 non-atomic으로 갱신하고, 겹치는 영역이 발급된다.
**조용히 데이터가 손상되는 use-after-free**다. manager가 참조를 들면 refcount가
0에 도달하는 시점이 "아무도 모름"과 동치가 되어 이 경합 자체가 사라진다.

### `_isOpen` 플래그 유지

배타성(스레드당 동시에 1개)을 요구하게 되고, 그 배타성을 강제하는 가드가
필요해진다. 예약을 Reserve 시점에 선점하면 겹침이 구조적으로 불가능해지므로
플래그·가드·그와 얽힌 버그가 통째로 사라진다.

## Consequences

- Commit 없이 조각이 소멸해도 프로세스가 죽지 않는다. 소멸자가 예약을 되돌린다.
- 중첩/인터리브 작성이 가능하다.
- **chunk pinning**: chunk는 거기서 나간 조각이 전부 해제돼야 반납된다. 응답이
  느린 클라이언트 하나가 작은 패킷을 붙잡으면 chunk 전체가 고정된다.
  [ADR-0002](0002-sendbuffer-sendview-split.md)의 브로드캐스트와 scatter/gather가
  이를 증폭시킨다. 이것이 chunk 크기 선택의 주된 제약이다.
- `_offset` 갱신 주체를 owner 하나로 묶기 위해, chunk 초기화(`Reset()`)는 Pool
  **반환** 시점이 아니라 **획득** 시점에 수행한다. 기능은 같지만 불변식이 단순해진다.
- 저장소를 `std::unique_ptr<std::byte[]>`로 두어 zero-fill을 없애고, 동시에 chunk
  용량을 생성 시점에 정할 수 있게 됐다. 덕분에 큰 요청도 "용량이 큰 chunk" 하나로
  균일하게 처리된다.
