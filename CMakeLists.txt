cmake_minimum_required(VERSION 3.16) # precompiled headers 지원
project(MyUtils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(MyUtils STATIC
    src/Network.cpp
    src/Memory.cpp
    include/MyUtils/Network.h
    include/MyUtils/Memory.h
)

target_precompile_headers(MyUtils PRIVATE src/pch.h)

# 별칭(Alias) 설정
add_library(MyUtils::MyUtils ALIAS MyUtils)

# 경로 설정
target_include_directories(MyUtils PUBLIC
    # 빌드할 때
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # 설치되거나 외부에서 쓸 때
    $<INSTALL_INTERFACE:include>
)