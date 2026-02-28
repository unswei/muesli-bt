cmake_minimum_required(VERSION 3.20)

project(muesli_bt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MUESLI_BT_BUILD_PYTHON_BRIDGE "Build the Python bridge module (muesli_bt_bridge)" OFF)

add_library(muesli_bt_core
    src/bt/blackboard.cpp
    src/bt/compiler.cpp
    src/bt/instance.cpp
    src/bt/logging.cpp
    src/bt/planner.cpp
    src/bt/profile.cpp
    src/bt/registry.cpp
    src/bt/runtime.cpp
    src/bt/runtime_host.cpp
    src/bt/scheduler.cpp
    src/bt/serialisation.cpp
    src/bt/status.cpp
    src/bt/trace.cpp
    src/bt/vla.cpp
    src/builtins.cpp
    src/env.cpp
    src/env_api.cpp
    src/env_builtins.cpp
    src/eval.cpp
    src/extensions.cpp
    src/gc.cpp
    src/printer.cpp
    src/reader.cpp
    src/value.cpp
)

target_include_directories(muesli_bt_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(muesli_bt_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(muesli_bt_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(muesli_bt_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(muesli_bt_ext_pybullet_racecar
    examples/pybullet_racecar/native/racecar_demo.cpp
    ext/pybullet_racecar/extension.cpp
)

target_link_libraries(muesli_bt_ext_pybullet_racecar PUBLIC muesli_bt_core)
target_include_directories(muesli_bt_ext_pybullet_racecar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/pybullet_racecar/native
        ${CMAKE_CURRENT_SOURCE_DIR}/ext
)
target_compile_features(muesli_bt_ext_pybullet_racecar PUBLIC cxx_std_20)

add_executable(muslisp src/main.cpp)
target_link_libraries(muslisp PRIVATE muesli_bt_core)

if(MUESLI_BT_BUILD_PYTHON_BRIDGE)
    if(NOT Python3_EXECUTABLE)
        set(MUESLI_BT_LOCAL_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv-py311/bin/python")
        if(EXISTS "${MUESLI_BT_LOCAL_PYTHON}")
            # Keep bridge/demo builds aligned with the pinned Python used by scripts/demo-setup.sh.
            set(Python3_EXECUTABLE "${MUESLI_BT_LOCAL_PYTHON}" CACHE FILEPATH "Python interpreter for muesli_bt bridge build" FORCE)
            if(NOT DEFINED Python3_FIND_VIRTUALENV)
                set(Python3_FIND_VIRTUALENV ONLY)
            endif()
            if(NOT DEFINED Python3_FIND_STRATEGY)
                set(Python3_FIND_STRATEGY LOCATION)
            endif()
            message(STATUS "MUESLI_BT_BUILD_PYTHON_BRIDGE: auto-selecting ${Python3_EXECUTABLE}")
        endif()
    endif()

    find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(pybind11 CONFIG QUIET)

    if(NOT pybind11_FOUND)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
            OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE PYBIND11_DIR_RESULT
        )
        if(PYBIND11_DIR_RESULT EQUAL 0 AND EXISTS "${PYBIND11_CMAKE_DIR}")
            list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_CMAKE_DIR}")
            find_package(pybind11 CONFIG REQUIRED)
        else()
            message(FATAL_ERROR
                "pybind11 not found for Python3_EXECUTABLE='${Python3_EXECUTABLE}'. "
                "Install it with '${Python3_EXECUTABLE} -m pip install pybind11' "
                "or run 'make demo-setup' to provision the pinned demo environment."
            )
        endif()
    endif()

    pybind11_add_module(muesli_bt_bridge examples/pybullet_racecar/native/bridge_module.cpp)
    target_link_libraries(muesli_bt_bridge PRIVATE muesli_bt_ext_pybullet_racecar)
    target_include_directories(muesli_bt_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(muesli_bt_bridge PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python")
endif()

enable_testing()

add_executable(muslisp_tests tests/test_main.cpp)
target_link_libraries(muslisp_tests PRIVATE muesli_bt_ext_pybullet_racecar)

add_test(NAME muslisp_tests COMMAND muslisp_tests)
