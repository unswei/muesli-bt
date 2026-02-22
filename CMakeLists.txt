cmake_minimum_required(VERSION 3.20)

project(muslisp_bt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(muslisp_core
    src/bt/blackboard.cpp
    src/bt/compiler.cpp
    src/bt/instance.cpp
    src/bt/logging.cpp
    src/bt/profile.cpp
    src/bt/registry.cpp
    src/bt/runtime.cpp
    src/bt/runtime_host.cpp
    src/bt/scheduler.cpp
    src/bt/status.cpp
    src/bt/trace.cpp
    src/builtins.cpp
    src/env.cpp
    src/eval.cpp
    src/gc.cpp
    src/printer.cpp
    src/reader.cpp
    src/value.cpp
)

target_include_directories(muslisp_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(muslisp_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(muslisp_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(muslisp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(muslisp src/main.cpp)
target_link_libraries(muslisp PRIVATE muslisp_core)

enable_testing()

add_executable(muslisp_tests tests/test_main.cpp)
target_link_libraries(muslisp_tests PRIVATE muslisp_core)

add_test(NAME muslisp_tests COMMAND muslisp_tests)
