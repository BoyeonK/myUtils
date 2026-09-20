# MyUtils가 /source-charset:utf-8 을 PUBLIC 으로 전파하므로 여기서 따로 지정할 필요가 없다.
# 실행 문자셋은 강제하지 않으므로 한글 출력은 시스템 코드 페이지로 인코딩된다.

add_executable(SendBufferTest SendBufferTest.cpp)
target_link_libraries(SendBufferTest PRIVATE MyUtils::MyUtils)
add_test(NAME SendBuffer COMMAND SendBufferTest)

add_executable(ActorTest ActorTest.cpp)
target_link_libraries(ActorTest PRIVATE MyUtils::MyUtils)
add_test(NAME Actor COMMAND ActorTest)
