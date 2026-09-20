# MyUtils가 /source-charset:utf-8 을 PUBLIC 으로 전파하므로 여기서 따로 지정할 필요가 없다.
# 실행 문자셋은 강제하지 않으므로 한글 출력은 시스템 코드 페이지로 인코딩된다.

add_executable(SendBufferTest SendBufferTest.cpp)
target_link_libraries(SendBufferTest PRIVATE MyUtils::MyUtils)
add_test(NAME SendBuffer COMMAND SendBufferTest)

# [주의] Actor 쪽 실행 파일이 넷으로 나뉜 것은 취향이 아니라 제약이다.
# 런타임이 프로세스 전역 단일이고 1회 기동·1회 종료라, 한 프로세스는 worker 수
# 하나와 종료 한 번만 가질 수 있다. 필요한 런타임 형상이 다르면 프로세스를 나눠야
# 한다. 어느 invariant가 어느 실행 파일로 갔는지는 design/actor-runtime.md §10.
add_executable(ActorTest ActorTest.cpp ActorTestSupport.h)
target_link_libraries(ActorTest PRIVATE MyUtils::MyUtils)
add_test(NAME Actor COMMAND ActorTest)

add_executable(ActorSingleWorkerTest ActorSingleWorkerTest.cpp ActorTestSupport.h)
target_link_libraries(ActorSingleWorkerTest PRIVATE MyUtils::MyUtils)
add_test(NAME ActorSingleWorker COMMAND ActorSingleWorkerTest)

add_executable(ActorShutdownTest ActorShutdownTest.cpp ActorTestSupport.h)
target_link_libraries(ActorShutdownTest PRIVATE MyUtils::MyUtils)
add_test(NAME ActorShutdown COMMAND ActorShutdownTest)

add_executable(RuntimeConfigTest RuntimeConfigTest.cpp ActorTestSupport.h)
target_link_libraries(RuntimeConfigTest PRIVATE MyUtils::MyUtils)
add_test(NAME RuntimeConfig COMMAND RuntimeConfigTest)
