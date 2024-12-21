cmake_minimum_required(VERSION 3.10)

project(selecon)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED on)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -gdwarf-4")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -gdwarf-4")

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt on cache bool "" force)
FetchContent_MakeAvailable(googletest)

enable_testing()

find_package(PkgConfig REQUIRED)
pkg_check_modules(libav REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    #libswresample
    #libswscale
    libavutil
)

file(GLOB SELECON_LIB_SOURCES src/selecon/*.c)
file(GLOB CLI_SOURCES src/selecon_cli/*.c)
file(GLOB TEST_SOURCES src/tests/*.cpp)

add_library(selecon ${SELECON_LIB_SOURCES})
target_include_directories(selecon INTERFACE src/selecon)
target_link_libraries(selecon PUBLIC avutil avformat avcodec avfilter)

add_executable(selecon_cli ${CLI_SOURCES})
target_link_libraries(selecon_cli selecon avdevice)

add_executable(unittests ${TEST_SOURCES})
target_link_libraries(unittests selecon GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(unittests)
