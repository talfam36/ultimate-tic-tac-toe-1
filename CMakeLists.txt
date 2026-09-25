cmake_minimum_required(VERSION 3.16)
project(uttt_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(uttt src/game.cpp src/dfpn.cpp src/tests.cpp)
target_include_directories(uttt PUBLIC src)
target_compile_options(uttt PRIVATE -O3 -march=native -Wall -Wextra -Wpedantic)

add_executable(uttt-solve src/main.cpp)
target_link_libraries(uttt-solve PRIVATE uttt)
target_compile_options(uttt-solve PRIVATE -O3 -march=native -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME self_test COMMAND uttt-solve --self-test --time 1)
