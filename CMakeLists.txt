cmake_minimum_required(VERSION 3.16) # precompiled headers 지원
project(MyUtils)

include(FetchContent)
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG        v1.0.4
)
FetchContent_MakeAvailable(concurrentqueue)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(MyUtils STATIC
    src/Network.cpp
    src/Memory.cpp
    src/Actor.cpp
    src/GlobalVariables.cpp

    include/MyUtils/Network.h
    include/MyUtils/Memory.h
    include/MyUtils/Actor.h
    include/MyUtils/GlobalVariables.h
)

target_precompile_headers(MyUtils PRIVATE src/pch.h)

target_link_libraries(MyUtils PUBLIC concurrentqueue)

# 별칭(Alias) 설정
add_library(MyUtils::MyUtils ALIAS MyUtils)

# 경로 설정
target_include_directories(MyUtils PUBLIC
    # 빌드할 때
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # 설치되거나 외부에서 쓸 때
    $<INSTALL_INTERFACE:include>
)