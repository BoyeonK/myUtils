# MyUtils

`MyUtils`는 내가 만드는 게임 서버를 비롯한 개인 프로젝트에서 재사용할 런타임 기반
요소를 모으는 C++17 정적 라이브러리다. 불특정 다수를 위한 범용 라이브러리나 실행 가능한
프레임워크보다는, 내 프로젝트에 필요한 작고 조합 가능한 부품을 만드는 데 초점을 둔다.

동시에 이 저장소는 AI 코딩 에이전트를 실제 개발 과정에 적용해 보는 학습 및 실험
프로젝트다.

이 저장소에는 두 가지 목적이 있다.

1. 내가 진행할 게임 관련 프로젝트에서 공통으로 사용할 라이브러리를 만든다.
2. Codex, Claude Code 등 여러 코딩 에이전트를 동시에 또는 교대로 활용하면서, 코드를
   설계·리뷰·테스트·유지보수하는 방법과 에이전트 간 협업 방식을 학습한다.

범용성을 위해 모든 기능을 모으기보다 내 프로젝트의 실제 사용 범위와 workload를
우선한다. 최적화는 그 범위에서 얻은 측정 결과를 근거로 진행하고, 성능보다 correctness를
먼저 확보한다. 중요한 설계 결정과 시행착오는 다른 에이전트나 미래의 내가 다시 추측하지
않도록 문서로 남긴다.

## 주요 구성 요소

| 구성 요소 | 공개 헤더 | 설명 |
|---|---|---|
| 송신 버퍼 | `MyUtils/SendBuffer.h` | thread-local bump allocator와 참조 카운트 chunk를 이용해 비동기 송신 데이터의 수명을 관리한다. `Reserve` → `Commit`으로 쓰기 영역을 불변 `SendView`로 전환한다. |
| Actor Runtime | `MyUtils/Actor.h` | MPSC mailbox, atomic 상태 머신, worker-local deque와 work stealing을 갖춘 경량 Actor 실행 환경이다. Actor별 message handler 실행을 직렬화한다. |

두 구성 요소 모두 자기 계측을 갖고 있으며 각자의 공개 헤더에서 `SnapshotStats()`로
조회한다. 비용이 chunk당 또는 배치당이라 항상 켜져 있고, 켜고 끄는 스위치는 없다.

라이브러리에는 애플리케이션 진입점이나 범용 스레드 관리 프레임워크가 없다. 각 구성 요소의
구체적인 계약과 제약은 공개 헤더 및 [`design/`](design/) 문서를 기준으로 한다.

## 프로젝트에 연결하기

필요 조건은 CMake 3.16 이상과 C++17 컴파일러다. 최초 configure 시
[`moodycamel/concurrentqueue`](https://github.com/cameron314/concurrentqueue)를
`FetchContent`로 가져오므로 Git과 네트워크 연결이 필요하다. 정확한 의존성 버전은
[`CMakeLists.txt`](CMakeLists.txt)를 기준으로 한다.

저장소를 프로젝트 안에 둔 경우:

```cmake
add_subdirectory(external/myUtils)

target_link_libraries(GameServer PRIVATE MyUtils::MyUtils)
```

Git 저장소에서 직접 가져오는 경우:

```cmake
include(FetchContent)

FetchContent_Declare(
    myutils
    GIT_REPOSITORY <MyUtils 저장소 URL>
    GIT_TAG        <고정할 커밋 또는 태그>
)
FetchContent_MakeAvailable(myutils)

target_link_libraries(GameServer PRIVATE MyUtils::MyUtils)
```

다른 프로젝트의 하위 디렉터리로 포함하면 MyUtils 테스트는 기본적으로 빌드하지 않는다.
필요하면 configure 전에 `MYUTILS_BUILD_TESTS`를 켤 수 있다.

## API 개요

송신 버퍼는 먼저 최대 크기를 예약하고, 실제로 기록한 크기만 commit한다.

```cpp
#include <MyUtils/SendBuffer.h>

#include <cstring>
#include <utility>

auto buffer = MyUtils::Network::SendBufferManager::Current().Reserve(payloadSize);
if (!buffer) {
    // 0바이트이거나 MAX_SEND_BUFFER_SIZE를 넘는 요청
    return;
}

std::memcpy(buffer.WritableData(), payload, payloadSize);
MyUtils::Network::SendView view = std::move(buffer).Commit(payloadSize);
```

`SendView`는 복사 가능한 읽기 전용 객체이며, 복사본이 살아 있는 동안 내부 chunk도 유지된다.

Actor는 `ActorSystem`이 소유하고 `ActorRef`를 통해서만 메시지를 받는다.

```cpp
#include <MyUtils/Actor.h>

#include <memory>
#include <utility>

struct Move final : MyUtils::Actors::Message {
    int x = 0;
    int y = 0;
};

class Player final : public MyUtils::Actors::Actor {
public:
    void Handle(MyUtils::Actors::Message& message) override {
        if (auto* move = dynamic_cast<Move*>(&message)) {
            _x = move->x;
            _y = move->y;
        }
    }

private:
    int _x = 0;
    int _y = 0;
};

MyUtils::Actors::ActorSystem actors(4);
auto player = actors.Spawn(std::make_unique<Player>());
auto move = std::make_unique<Move>();
move->x = 10;
move->y = 20;
player.Send(std::move(move));
```

`ActorRef::Send()`의 `true`는 메시지가 접수되었다는 뜻이며 최종 처리를 보장하지 않는다.
이후 `Stop()`이나 `Shutdown()`이 pending 메시지를 버릴 수 있다. 자세한 실행 규약은
[`design/actor-runtime.md`](design/actor-runtime.md)에 있다.

계측값은 다음과 같이 조회한다.

```cpp
#include <MyUtils/Actor.h>
#include <MyUtils/SendBuffer.h>

// 측정할 workload를 실행한 뒤 통계를 조회한다.
auto actorStats = MyUtils::Actors::SnapshotStats();
auto bufferStats = MyUtils::Network::SnapshotStats();
```

## 빌드와 테스트

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

멀티 컨피그 생성기에서는 `--config`와 `-C`가 필요하다.

## 여러 에이전트와 함께 개발하기

에이전트마다 독립적인 대화 맥락을 갖더라도 같은 결론에 도달할 수 있도록, 정보의 역할을
나누어 저장한다.

- [`AGENTS.md`](AGENTS.md): Codex가 저장소에 진입할 때 읽는 안내
- [`CLAUDE.md`](CLAUDE.md): 모든 코딩 에이전트가 공유하는 현재 프로젝트 문맥과 작업 규칙
- [`design/`](design/): 여러 파일에 걸친 실행 규약과 컴포넌트 설계
- [`design/DOC_RULES.md`](design/DOC_RULES.md): 무엇을 어느 문서에 쓰는가
- [`progress.md`](progress.md): 완료 이력, 하기로 정해진 후속 작업, 알려진 이슈

**"왜 그렇게 정했는가"는 저장하지 않는다.** 기록하는 것은 현재 동작과 "무엇을 건드리면
깨지는가"뿐이며, 전자는 코드 주석과 `design/`에, 후자는 그 옆에 함께 둔다.

코드 변경은 관련 계약을 먼저 확인하고, 구현 후 해당 invariant를 검증하는 테스트를
실행하는 흐름을 따른다. README에는 오래 유지되는 프로젝트 목적과 사용법만 적고, 수시로
바뀌는 진행 상태는 [`progress.md`](progress.md)에서 관리한다. 아직 정하지 못한 것은
문서에 쌓지 않고 결정해서 현재 동작 서술에 반영한다.
