add_executable(SendBufferTest SendBufferTest.cpp)

# MyUtils가 /utf-8 을 PUBLIC 으로 전파하므로 여기서 따로 지정할 필요가 없다.
target_link_libraries(SendBufferTest PRIVATE MyUtils::MyUtils)

add_test(NAME SendBuffer COMMAND SendBufferTest)
