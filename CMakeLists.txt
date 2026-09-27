cmake_minimum_required(VERSION 3.16)
project(ballast C)

set(CMAKE_C_STANDARD 11)
find_package(SDL3 REQUIRED CONFIG)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_executable(ballast ${SOURCES})
target_link_libraries(ballast PRIVATE SDL3::SDL3)
if (NOT MSVC)
    target_link_libraries(ballast PRIVATE m)
    target_compile_options(ballast PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
