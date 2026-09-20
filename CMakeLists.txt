cmake_minimum_required(VERSION 3.16)
project(MyUtils)

include(FetchContent)
FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG        v1.0.4
)
FetchContent_MakeAvailable(concurrentqueue)




# [주의] 목록을 file(GLOB)으로 바꾸지 말 것. CMake가 파일 추가를 감지하지 못해
# 새 파일이 빠진 채로 빌드가 성공한다(CONFIGURE_DEPENDS도 생성기에 따라 보장되지
# 않는다). 새 소스는 여기에 직접 넣는다 -- 빠뜨리면 링커가 잡아준다.
# 헤더도 함께 적혀 있는데 그쪽은 IDE 표시용이라 빌드에는 영향이 없다.
add_library(MyUtils STATIC
    src/Assert.cpp
    src/SendBuffer.cpp
    src/Runtime.cpp
    src/Actors.cpp

    include/MyUtils/SendBuffer.h
    include/MyUtils/Actor.h
    include/MyUtils/Runtime.h
    include/MyUtils/Assert.h
    src/RuntimeInternal.h
)

# [주의] C++17 요구를 타깃에 싣는다. set(CMAKE_CXX_STANDARD 17)은 디렉터리 스코프
# 변수라 add_subdirectory로 가져다 쓰는 소비자에게 전파되지 않는다. 공개 헤더가 중첩
# 네임스페이스·std::byte·inline constexpr을 쓰므로, 이게 없으면 소비자 타깃이 컴파일러
# 기본 표준(MSVC는 C++14)으로 공개 헤더를 컴파일하다 C2429로 실패한다.
target_compile_features(MyUtils PUBLIC cxx_std_17)

# 소스와 공개 헤더는 BOM 없는 UTF-8이다. MSVC만 기본값이 시스템 코드 페이지(한국어
# 환경이면 949)라, 한글 주석의 마지막 바이트가 lead byte로 오해되어 줄바꿈까지 삼키고
# 다음 줄이 주석에 먹히는 일이 실제로 발생한다. gcc/clang은 UTF-8이 기본이라 필요 없다.
#
# 공개 헤더에도 한글 주석이 있으므로 PUBLIC으로 전파하되, 소비자 타깃의 컴파일러로
# 판정되도록 제너레이터 표현식을 쓴다. if(MSVC)로 쓰면 이 프로젝트를 configure한
# 컴파일러로 결정이 굳어 그대로 소비자에게 넘어간다.
#
# [주의] /utf-8이 아니라 /source-charset:utf-8이다. 전자는 실행 문자셋까지 UTF-8로
# 강제해 소비자 TU 전체의 리터럴 인코딩을 이쪽에서 정해버린다. 헤더를 올바로 읽히는 데
# 필요한 것은 소스 문자셋뿐이다.
target_compile_options(MyUtils
    PUBLIC
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/source-charset:utf-8>
)

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