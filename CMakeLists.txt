cmake_minimum_required(VERSION 3.14) # FetchContent는 3.11+ 필요
project(MyUtils)

# 1. 라이브러리 생성 (STATIC 추천)
add_library(MyUtils STATIC
    src/Network.cpp
    include/MyUtils/Network.h
)

# 2. 별칭(Alias) 설정
# 남들이 쓸 때 'MyUtils::MyUtils'라는 이름으로 찾게 해줍니다.
# 이렇게 하면 이름 충돌을 방지할 수 있습니다.
add_library(MyUtils::MyUtils ALIAS MyUtils)

# 3. 인클루드 경로 설정 (Modern CMake 방식)
target_include_directories(MyUtils PUBLIC
    # 빌드할 때(내 컴퓨터)는 이 경로를 쓰고
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # 설치되거나 외부에서 쓸 때는 include 폴더를 참조해라
    $<INSTALL_INTERFACE:include>
)