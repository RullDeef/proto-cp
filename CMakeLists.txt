cmake_minimum_required(VERSION 3.10)

project(selecon)

set(CMAKE_C_STANDARD 11)
SET(CMAKE_C_STANDARD_REQUIRED on)

file(GLOB SELECON_LIB_SOURCES src/selecon/*.c)
file(GLOB EXAMPLE_SOURCES src/example/*.c)

add_library(selecon ${SELECON_LIB_SOURCES})
target_include_directories(selecon INTERFACE src/selecon)

add_executable(example ${EXAMPLE_SOURCES})
target_link_libraries(example selecon)

