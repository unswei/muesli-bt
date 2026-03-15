set(_muesli_bt_bench_build_type "${CMAKE_BUILD_TYPE}")
if (_muesli_bt_bench_build_type STREQUAL "")
  set(_muesli_bt_bench_build_type "unknown")
endif ()

set(_muesli_bt_bench_git_commit "unknown")
find_package(Git QUIET)
if (Git_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _muesli_bt_bench_git_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif ()

set(_muesli_bt_bench_build_flags "${CMAKE_CXX_FLAGS}")
string(TOUPPER "${CMAKE_BUILD_TYPE}" _muesli_bt_bench_build_type_upper)
if (DEFINED CMAKE_CXX_FLAGS_${_muesli_bt_bench_build_type_upper})
  string(APPEND _muesli_bt_bench_build_flags " ${CMAKE_CXX_FLAGS_${_muesli_bt_bench_build_type_upper}}")
endif ()
string(STRIP "${_muesli_bt_bench_build_flags}" _muesli_bt_bench_build_flags)

set(_muesli_bt_bench_btcpp_version "")
set(_muesli_bt_bench_btcpp_commit "")
set(_muesli_bt_bench_with_btcpp 0)

if (MUESLI_BT_BENCH_WITH_BTCPP)
  include(FetchContent)

  set(_muesli_bt_bench_btcpp_version "4.9.0")
  set(_muesli_bt_bench_btcpp_commit "3ff6a32ba0497a08519c77a1436e3b81eff1bcd6")
  set(_muesli_bt_bench_with_btcpp 1)

  set(BTCPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(BTCPP_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(BTCPP_GROOT_INTERFACE OFF CACHE BOOL "" FORCE)
  set(BTCPP_SQLITE_LOGGING OFF CACHE BOOL "" FORCE)
  set(USE_VENDORED_CPPZMQ ON CACHE BOOL "" FORCE)
  set(USE_VENDORED_FLATBUFFERS ON CACHE BOOL "" FORCE)
  set(USE_VENDORED_MINICORO ON CACHE BOOL "" FORCE)
  set(USE_VENDORED_MINITRACE ON CACHE BOOL "" FORCE)
  set(USE_VENDORED_TINYXML2 ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    behaviortree_cpp
    GIT_REPOSITORY https://github.com/BehaviorTree/BehaviorTree.CPP.git
    GIT_TAG ${_muesli_bt_bench_btcpp_commit}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(behaviortree_cpp)
endif ()

if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(WARNING "MUESLI_BT_BUILD_BENCHMARKS is intended for Release builds. Current build type: ${_muesli_bt_bench_build_type}")
endif ()

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/bench_config.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/bench_config.hpp"
  @ONLY
)

add_library(
  muesli_bt_bench_harness
  harness/allocation_tracker.cpp
  harness/csv_writer.cpp
  harness/metadata.cpp
  harness/runner.cpp
  harness/scenario.cpp
  harness/stats.cpp
  fixtures/schedules.cpp
  fixtures/source_factory.cpp
  fixtures/tree_factory.cpp
  runtimes/muesli_adapter.cpp
)

if (MUESLI_BT_BENCH_WITH_BTCPP)
  target_sources(muesli_bt_bench_harness PRIVATE runtimes/btcpp_adapter.cpp)
endif ()

target_link_libraries(muesli_bt_bench_harness PUBLIC muesli_bt_core)
if (MUESLI_BT_BENCH_WITH_BTCPP)
  target_link_libraries(muesli_bt_bench_harness PUBLIC behaviortree_cpp)
endif ()
target_include_directories(
  muesli_bt_bench_harness
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(muesli_bt_bench_harness PUBLIC cxx_std_20)

if (MSVC)
  target_compile_options(muesli_bt_bench_harness PRIVATE /W4 /permissive-)
else ()
  target_compile_options(muesli_bt_bench_harness PRIVATE -Wall -Wextra -Wpedantic)
endif ()

add_executable(muesli_bt_bench main.cpp)
target_link_libraries(muesli_bt_bench PRIVATE muesli_bt_bench_harness)
set_target_properties(muesli_bt_bench PROPERTIES OUTPUT_NAME bench)

add_executable(muesli_bt_bench_tests tests/test_main.cpp)
target_link_libraries(muesli_bt_bench_tests PRIVATE muesli_bt_bench_harness)
add_test(NAME muesli_bt_bench_tests COMMAND muesli_bt_bench_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
  add_test(
    NAME muesli_bt_bench_analysis_smoke
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_analyse_results.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/analyse_results.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/analysis_sample"
  )
endif ()
