cmake_minimum_required(VERSION 3.16)
project(neon_descent C)

set(CMAKE_C_STANDARD 11)
find_package(SDL3 REQUIRED CONFIG)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_executable(neon_descent ${SOURCES})
target_link_libraries(neon_descent PRIVATE SDL3::SDL3)
if (NOT MSVC)
    target_link_libraries(neon_descent PRIVATE m)
    target_compile_options(neon_descent PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
