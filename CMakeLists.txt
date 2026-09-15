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
    src/SendBuffer.cpp
    src/GlobalVariables.cpp
    src/Thread.cpp

    include/MyUtils/SendBuffer.h
    include/MyUtils/Assert.h
    include/MyUtils/GlobalVariables.h
    include/MyUtils/Thread.h
)

# 소스와 헤더는 전부 UTF-8이다. MSVC는 기본적으로 시스템 코드 페이지(한국어
# 환경이면 949)로 읽기 때문에, 한글 주석의 마지막 바이트가 lead byte로 오해되어
# 줄바꿈까지 삼키고 다음 줄이 주석에 먹히는 일이 실제로 발생한다.
# 공개 헤더에도 한글 주석이 있으므로 PUBLIC으로 전파한다.
if(MSVC)
    target_compile_options(MyUtils PUBLIC /utf-8)
endif()

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

# 테스트
# 단독으로 빌드할 때만 기본으로 켠다. 다른 프로젝트가 add_subdirectory로
# 가져다 쓸 때는 테스트까지 빌드되지 않도록 한다.
# (PROJECT_IS_TOP_LEVEL은 CMake 3.21+ 라서 직접 비교한다)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MYUTILS_DEFAULT_BUILD_TESTS ON)
else()
    set(MYUTILS_DEFAULT_BUILD_TESTS OFF)
endif()

option(MYUTILS_BUILD_TESTS "MyUtils 테스트를 빌드한다" ${MYUTILS_DEFAULT_BUILD_TESTS})

if(MYUTILS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()