cmake_minimum_required(VERSION 3.20)

project(
  muesli_bt
  VERSION 0.8.0
  LANGUAGES C CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MUESLI_BT_BUILD_PYTHON_BRIDGE "Build the Python bridge module (muesli_bt_bridge)" OFF)
option(MUESLI_BT_BUILD_WEBOTS_EXAMPLES "Build the Webots e-puck example controllers" OFF)
option(MUESLI_BT_BUILD_INTEGRATION_PYBULLET "Build the PyBullet integration library" ON)
option(MUESLI_BT_BUILD_INTEGRATION_WEBOTS "Build the Webots integration library" ON)
option(MUESLI_BT_BUILD_INTEGRATION_ROS2 "Build the ROS2 integration library" OFF)
option(MUESLI_BT_BUILD_INTEGRATION_BOOSTER "Build the SDK-independent Booster host bridge" ON)
option(MUESLI_BT_BOOSTER_PORTABLE_RUNNER
       "Link the humanoid Booster runner with portable GNU C++ runtime libraries" OFF)
option(MUESLI_BT_BUILD_MODEL_SERVICE_BRIDGE "Build the optional muesli-model-service bridge target" OFF)
option(MUESLI_BT_BUILD_BENCHMARKS "Build the optional benchmark harness" OFF)
option(MUESLI_BT_BENCH_WITH_BTCPP "Build the optional BehaviorTree.CPP benchmark adapter" OFF)
option(MUESLI_BT_BUILD_CONTROLLED_AUTHORITY_EXPERIMENT
       "Build the controlled invocation-authority experiment" ON)

add_library(
  muesli_bt_core
  src/bt/approach_pose_validator.cpp
  src/bt/blackboard.cpp
  src/bt/compiler.cpp
  src/bt/event_payload.cpp
  src/bt/event_log.cpp
  src/bt/instance.cpp
  src/bt/logging.cpp
  src/bt/model_service.cpp
  src/bt/node_options.cpp
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
  src/cap_api.cpp
  src/compiled_eval.cpp
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

target_include_directories(
  muesli_bt_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(muesli_bt_core PROPERTIES EXPORT_NAME runtime POSITION_INDEPENDENT_CODE ON)

target_compile_features(muesli_bt_core PUBLIC cxx_std_20)

if (MSVC)
  target_compile_options(muesli_bt_core PRIVATE /W4 /permissive-)
else ()
  target_compile_options(muesli_bt_core PRIVATE -Wall -Wextra -Wpedantic)
endif ()

if (MUESLI_BT_BUILD_INTEGRATION_PYBULLET)
  add_library(
    muesli_bt_integration_pybullet integrations/pybullet/extension.cpp
                                   integrations/pybullet/racecar_demo.cpp
  )

  target_link_libraries(muesli_bt_integration_pybullet PUBLIC muesli_bt_core)
  target_include_directories(
    muesli_bt_integration_pybullet
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/integrations>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(
    muesli_bt_integration_pybullet PROPERTIES EXPORT_NAME integration_pybullet
                                                 POSITION_INDEPENDENT_CODE ON
  )
  target_compile_features(muesli_bt_integration_pybullet PUBLIC cxx_std_20)
endif ()

if (MUESLI_BT_BUILD_MODEL_SERVICE_BRIDGE)
  target_compile_definitions(muesli_bt_core PUBLIC MUESLI_BT_WITH_MODEL_SERVICE_BRIDGE=1)
  add_library(muesli_bt_integration_model_service integrations/model_service/bridge.cpp)
  target_link_libraries(muesli_bt_integration_model_service PUBLIC muesli_bt_core)
  target_include_directories(
    muesli_bt_integration_model_service
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/integrations>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_compile_features(muesli_bt_integration_model_service PUBLIC cxx_std_20)
  set_target_properties(
    muesli_bt_integration_model_service PROPERTIES EXPORT_NAME integration_model_service
                                                   POSITION_INDEPENDENT_CODE ON
  )
  target_compile_definitions(muesli_bt_integration_model_service
                             PUBLIC MUESLI_BT_WITH_MODEL_SERVICE_BRIDGE=1)
endif ()

if (MUESLI_BT_BUILD_INTEGRATION_BOOSTER)
  add_library(muesli_bt_integration_booster integrations/booster/bridge_client.cpp)
  target_link_libraries(muesli_bt_integration_booster PUBLIC muesli_bt_core)
  target_include_directories(
    muesli_bt_integration_booster
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/integrations>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(
    muesli_bt_integration_booster PROPERTIES EXPORT_NAME integration_booster
                                                POSITION_INDEPENDENT_CODE ON
  )
  target_compile_features(muesli_bt_integration_booster PUBLIC cxx_std_20)
endif ()

set(MUESLI_BT_HAVE_WEBOTS OFF)
set(MUESLI_BT_WEBOTS_RPATH_ROOT "")
if (MUESLI_BT_BUILD_INTEGRATION_WEBOTS OR MUESLI_BT_BUILD_WEBOTS_EXAMPLES)
  find_path(
    WEBOTS_CPP_INCLUDE_DIR webots/Robot.hpp
    HINTS ENV WEBOTS_HOME "${WEBOTS_HOME}"
    PATH_SUFFIXES include/controller/cpp Contents/include/controller/cpp
  )
  find_library(
    WEBOTS_CONTROLLER_LIBRARY Controller
    HINTS ENV WEBOTS_HOME "${WEBOTS_HOME}"
    PATH_SUFFIXES lib/controller Contents/lib/controller
  )
  find_library(
    WEBOTS_CPP_CONTROLLER_LIBRARY CppController
    HINTS ENV WEBOTS_HOME "${WEBOTS_HOME}"
    PATH_SUFFIXES lib/controller Contents/lib/controller
  )

  if (WEBOTS_CPP_INCLUDE_DIR
      AND WEBOTS_CONTROLLER_LIBRARY
      AND WEBOTS_CPP_CONTROLLER_LIBRARY
  )
    set(MUESLI_BT_HAVE_WEBOTS ON)
    if (APPLE)
      if (WEBOTS_CPP_CONTROLLER_LIBRARY MATCHES "^(.+)/Contents/lib/controller/[^/]+$")
        set(MUESLI_BT_WEBOTS_RPATH_ROOT "${CMAKE_MATCH_1}")
      elseif (WEBOTS_CPP_INCLUDE_DIR MATCHES "^(.+)/Contents/include/controller/cpp$")
        set(MUESLI_BT_WEBOTS_RPATH_ROOT "${CMAKE_MATCH_1}")
      elseif (
        DEFINED ENV{WEBOTS_HOME}
        AND NOT
            "$ENV{WEBOTS_HOME}"
            STREQUAL
            ""
      )
        set(MUESLI_BT_WEBOTS_RPATH_ROOT "$ENV{WEBOTS_HOME}")
      endif ()

      if (MUESLI_BT_WEBOTS_RPATH_ROOT STREQUAL "")
        get_filename_component(_webots_controller_dir "${WEBOTS_CPP_CONTROLLER_LIBRARY}" DIRECTORY)
        get_filename_component(_webots_lib_dir "${_webots_controller_dir}" DIRECTORY)
        get_filename_component(_webots_contents_dir "${_webots_lib_dir}" DIRECTORY)
        get_filename_component(MUESLI_BT_WEBOTS_RPATH_ROOT "${_webots_contents_dir}" DIRECTORY)
      endif ()

      if (MUESLI_BT_WEBOTS_RPATH_ROOT MATCHES "/Contents$")
        get_filename_component(
          MUESLI_BT_WEBOTS_RPATH_ROOT "${MUESLI_BT_WEBOTS_RPATH_ROOT}" DIRECTORY
        )
      endif ()
    endif ()
  endif ()
endif ()

if (MUESLI_BT_BUILD_INTEGRATION_WEBOTS AND NOT MUESLI_BT_HAVE_WEBOTS)
  message(WARNING "MUESLI_BT_BUILD_INTEGRATION_WEBOTS=ON but Webots SDK was not found. "
                  "The integration_webots target will be omitted. Set WEBOTS_HOME to enable it."
  )
endif ()

if (MUESLI_BT_BUILD_INTEGRATION_WEBOTS AND MUESLI_BT_HAVE_WEBOTS)
  add_library(muesli_bt_integration_webots integrations/webots/extension.cpp)

  target_link_libraries(
    muesli_bt_integration_webots PUBLIC muesli_bt_core ${WEBOTS_CPP_CONTROLLER_LIBRARY}
                                        ${WEBOTS_CONTROLLER_LIBRARY}
  )
  target_include_directories(
    muesli_bt_integration_webots
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/integrations>
           $<BUILD_INTERFACE:${WEBOTS_CPP_INCLUDE_DIR}>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(
    muesli_bt_integration_webots PROPERTIES EXPORT_NAME integration_webots
                                               POSITION_INDEPENDENT_CODE ON
  )
  target_compile_features(muesli_bt_integration_webots PUBLIC cxx_std_20)
  if (APPLE)
    if (MUESLI_BT_WEBOTS_RPATH_ROOT STREQUAL "")
      message(FATAL_ERROR "Unable to derive Webots runtime rpath root for macOS integration export")
    endif ()
    target_link_options(
      muesli_bt_integration_webots INTERFACE "LINKER:-rpath,${MUESLI_BT_WEBOTS_RPATH_ROOT}"
    )
  endif ()
endif ()

if (MUESLI_BT_BUILD_INTEGRATION_ROS2)
  if (NOT UNIX OR APPLE)
    message(
      FATAL_ERROR
        "MUESLI_BT_BUILD_INTEGRATION_ROS2=ON requires a Linux ROS 2 environment. "
        "Use Ubuntu 22.04 + Humble for the first supported lane."
    )
  endif ()

  find_package(rclcpp REQUIRED)
  find_package(rclcpp_action REQUIRED)
  find_package(nav_msgs REQUIRED)
  find_package(nav2_msgs REQUIRED)
  find_package(geometry_msgs REQUIRED)

  add_library(
    muesli_bt_integration_ros2
    integrations/ros2/backend.cpp
    integrations/ros2/extension.cpp
    integrations/ros2/nav2_capability.cpp
  )

  target_link_libraries(
    muesli_bt_integration_ros2
    PUBLIC muesli_bt_core rclcpp::rclcpp rclcpp_action::rclcpp_action nav_msgs::nav_msgs__rosidl_generator_cpp
           nav_msgs::nav_msgs__rosidl_typesupport_cpp geometry_msgs::geometry_msgs__rosidl_generator_cpp
           geometry_msgs::geometry_msgs__rosidl_typesupport_cpp nav2_msgs::nav2_msgs__rosidl_generator_cpp
           nav2_msgs::nav2_msgs__rosidl_typesupport_cpp
  )
  target_include_directories(
    muesli_bt_integration_ros2
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/integrations>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(
    muesli_bt_integration_ros2 PROPERTIES EXPORT_NAME integration_ros2
                                             POSITION_INDEPENDENT_CODE ON
  )
  target_compile_features(muesli_bt_integration_ros2 PUBLIC cxx_std_20)
endif ()

add_executable(muslisp src/main.cpp src/repl_support.cpp)
target_link_libraries(muslisp PRIVATE muesli_bt_core)
if (TARGET muesli_bt_integration_model_service)
  target_link_libraries(muslisp PRIVATE muesli_bt_integration_model_service)
endif ()
if (UNIX)
  target_sources(muslisp PRIVATE third_party/linenoise/linenoise.c)
  target_include_directories(muslisp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/linenoise)
  target_compile_definitions(muslisp PRIVATE MUESLI_BT_HAVE_LINENOISE=1)
endif ()

add_executable(
  humanoid_model_mediated_trial
  examples/humanoid_model_mediated_approach/src/delayed_fake_service.cpp
  examples/humanoid_model_mediated_approach/src/experiment_runner.cpp
)
target_include_directories(
  humanoid_model_mediated_trial
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples/humanoid_model_mediated_approach/src
)
if (TARGET muesli_bt_integration_booster)
  target_link_libraries(humanoid_model_mediated_trial PRIVATE muesli_bt_integration_booster)
  target_compile_definitions(humanoid_model_mediated_trial PRIVATE MUESLI_BT_HAVE_BOOSTER_BRIDGE=1)
else ()
  target_link_libraries(humanoid_model_mediated_trial PRIVATE muesli_bt_core)
endif ()
if (MUESLI_BT_BOOSTER_PORTABLE_RUNNER)
  if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR NOT UNIX OR APPLE)
    message(FATAL_ERROR "MUESLI_BT_BOOSTER_PORTABLE_RUNNER requires GNU C++ on Linux")
  endif ()
  target_link_options(humanoid_model_mediated_trial PRIVATE -static-libgcc -static-libstdc++)
endif ()

if (UNIX)
  add_library(
    muesli_bt_air_hockey_example
    examples/air_hockey_model_mediated_defence/src/action_gate.cpp
    examples/air_hockey_model_mediated_defence/src/env_backend.cpp
    examples/air_hockey_model_mediated_defence/src/host_client.cpp
  )
  target_include_directories(
    muesli_bt_air_hockey_example
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/examples/air_hockey_model_mediated_defence/src
  )
  target_link_libraries(muesli_bt_air_hockey_example PUBLIC muesli_bt_core)

  add_executable(
    muesli_bt_air_hockey_scenario_tests
    examples/air_hockey_model_mediated_defence/tests/scenario_tests.cpp
  )
  target_link_libraries(
    muesli_bt_air_hockey_scenario_tests PRIVATE muesli_bt_air_hockey_example
  )
endif ()

if (TARGET muesli_bt_integration_ros2)
  add_executable(muslisp_ros2 tools/muslisp_ros2_main.cpp)
  target_link_libraries(muslisp_ros2 PRIVATE muesli_bt_integration_ros2)
endif ()

set(MUESLI_BT_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/muesli_bt")
set(MUESLI_BT_INSTALL_SHAREDIR "${CMAKE_INSTALL_DATADIR}/muesli_bt")

if (MUESLI_BT_BUILD_WEBOTS_EXAMPLES)
  if (NOT MUESLI_BT_HAVE_WEBOTS)
    message(
      FATAL_ERROR
        "MUESLI_BT_BUILD_WEBOTS_EXAMPLES=ON requires Webots controller headers and library. "
        "Set WEBOTS_HOME to your Webots installation path."
    )
  endif ()

  function (
    add_webots_epuck_controller
    target_name
    source_path
    output_dir
  )
    add_executable(${target_name} ${source_path})
    target_link_libraries(
      ${target_name} PRIVATE muesli_bt_core ${WEBOTS_CPP_CONTROLLER_LIBRARY}
                             ${WEBOTS_CONTROLLER_LIBRARY}
    )
    target_include_directories(
      ${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${WEBOTS_CPP_INCLUDE_DIR}
    )
    target_compile_features(${target_name} PRIVATE cxx_std_20)
    set_target_properties(
      ${target_name} PROPERTIES OUTPUT_NAME muesli_epuck RUNTIME_OUTPUT_DIRECTORY ${output_dir}
    )
  endfunction ()

  add_webots_epuck_controller(
    muesli_webots_epuck_obstacle
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_obstacle/controllers/muesli_epuck/muesli_epuck.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_obstacle/controllers/muesli_epuck
  )
  add_webots_epuck_controller(
    muesli_webots_epuck_line
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_line/controllers/muesli_epuck/muesli_epuck.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_line/controllers/muesli_epuck
  )
  add_webots_epuck_controller(
    muesli_webots_epuck_goal
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_goal/controllers/muesli_epuck/muesli_epuck.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_goal/controllers/muesli_epuck
  )
  add_webots_epuck_controller(
    muesli_webots_epuck_foraging
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_foraging/controllers/muesli_epuck/muesli_epuck.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_foraging/controllers/muesli_epuck
  )
  add_webots_epuck_controller(
    muesli_webots_epuck_tag
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_tag/controllers/muesli_epuck/muesli_epuck.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/webots_epuck_tag/controllers/muesli_epuck
  )
endif ()

if (MUESLI_BT_BUILD_PYTHON_BRIDGE)
  if (NOT MUESLI_BT_BUILD_INTEGRATION_PYBULLET)
    message(
      FATAL_ERROR
        "MUESLI_BT_BUILD_PYTHON_BRIDGE=ON requires MUESLI_BT_BUILD_INTEGRATION_PYBULLET=ON"
    )
  endif ()

  if (NOT Python3_EXECUTABLE)
    set(MUESLI_BT_LOCAL_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv-py311/bin/python")
    if (EXISTS "${MUESLI_BT_LOCAL_PYTHON}")
      # Keep bridge/demo builds aligned with the pinned Python used by scripts/demo-setup.sh.
      set(Python3_EXECUTABLE
          "${MUESLI_BT_LOCAL_PYTHON}"
          CACHE FILEPATH "Python interpreter for muesli_bt bridge build" FORCE
      )
      if (NOT DEFINED Python3_FIND_VIRTUALENV)
        set(Python3_FIND_VIRTUALENV ONLY)
      endif ()
      if (NOT DEFINED Python3_FIND_STRATEGY)
        set(Python3_FIND_STRATEGY LOCATION)
      endif ()
      message(STATUS "MUESLI_BT_BUILD_PYTHON_BRIDGE: auto-selecting ${Python3_EXECUTABLE}")
    endif ()
  endif ()

  find_package(
    Python3
    COMPONENTS Interpreter Development.Module
    REQUIRED
  )
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE PYBIND11_DIR_RESULT
  )
  if (PYBIND11_DIR_RESULT EQUAL 0 AND EXISTS "${PYBIND11_CMAKE_DIR}")
    set(pybind11_DIR "${PYBIND11_CMAKE_DIR}" CACHE PATH "pybind11 CMake package directory" FORCE)
    list(PREPEND CMAKE_PREFIX_PATH "${PYBIND11_CMAKE_DIR}")
  endif ()

  find_package(pybind11 CONFIG QUIET)

  if (NOT pybind11_FOUND)
    if (PYBIND11_DIR_RESULT EQUAL 0 AND EXISTS "${PYBIND11_CMAKE_DIR}")
      find_package(pybind11 CONFIG REQUIRED)
    else ()
      message(
        FATAL_ERROR
          "pybind11 not found for Python3_EXECUTABLE='${Python3_EXECUTABLE}'. "
          "Install it with '${Python3_EXECUTABLE} -m pip install pybind11' "
          "or run 'make demo-setup' to provision the pinned demo environment."
      )
    endif ()
  endif ()

  pybind11_add_module(muesli_bt_bridge examples/pybullet_racecar/native/bridge_module.cpp)
  target_link_libraries(muesli_bt_bridge PRIVATE muesli_bt_integration_pybullet)
  target_include_directories(
    muesli_bt_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                             ${CMAKE_CURRENT_SOURCE_DIR}/integrations
  )
  set_target_properties(
    muesli_bt_bridge PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
  )
endif ()

enable_testing()

if (MUESLI_BT_BUILD_CONTROLLED_AUTHORITY_EXPERIMENT)
  add_library(
    muesli_bt_controlled_authority_common_task
    experiments/invocation_authority_controlled/src/common_task.cpp
  )
  target_include_directories(
    muesli_bt_controlled_authority_common_task
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/experiments/invocation_authority_controlled/src
  )
  target_compile_features(muesli_bt_controlled_authority_common_task PUBLIC cxx_std_20)
  if (MSVC)
    target_compile_options(muesli_bt_controlled_authority_common_task PRIVATE /W4 /permissive-)
  else ()
    target_compile_options(
      muesli_bt_controlled_authority_common_task PRIVATE -Wall -Wextra -Wpedantic
    )
  endif ()

  add_library(
    muesli_bt_controlled_authority_variants
    experiments/invocation_authority_controlled/src/effect_recorder.cpp
    experiments/invocation_authority_controlled/src/variant.cpp
  )
  target_include_directories(
    muesli_bt_controlled_authority_variants
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/experiments/invocation_authority_controlled/src
  )
  target_link_libraries(
    muesli_bt_controlled_authority_variants
    PUBLIC muesli_bt_controlled_authority_common_task
  )
  target_compile_features(muesli_bt_controlled_authority_variants PUBLIC cxx_std_20)
  if (MSVC)
    target_compile_options(muesli_bt_controlled_authority_variants PRIVATE /W4 /permissive-)
  else ()
    target_compile_options(muesli_bt_controlled_authority_variants PRIVATE -Wall -Wextra -Wpedantic)
  endif ()

  add_executable(
    muesli_bt_controlled_authority_common_task_tests
    experiments/invocation_authority_controlled/tests/common_task_tests.cpp
  )
  target_link_libraries(
    muesli_bt_controlled_authority_common_task_tests
    PRIVATE muesli_bt_controlled_authority_common_task muesli_bt_core
  )
  target_compile_definitions(
    muesli_bt_controlled_authority_common_task_tests
    PRIVATE
      MUESLI_BT_CONTROLLED_AUTHORITY_COMMON_TREE="${CMAKE_CURRENT_SOURCE_DIR}/experiments/invocation_authority_controlled/lisp/common_task.lisp"
  )
  add_test(
    NAME muesli_bt_controlled_authority_common_task
    COMMAND muesli_bt_controlled_authority_common_task_tests
  )

  add_executable(
    muesli_bt_controlled_authority_variant_tests
    experiments/invocation_authority_controlled/tests/variant_tests.cpp
  )
  target_link_libraries(
    muesli_bt_controlled_authority_variant_tests
    PRIVATE muesli_bt_controlled_authority_variants
  )
  add_test(
    NAME muesli_bt_controlled_authority_variants
    COMMAND muesli_bt_controlled_authority_variant_tests
  )

  add_library(
    muesli_bt_controlled_authority_runner
    experiments/invocation_authority_controlled/src/runtime_variant.cpp
    experiments/invocation_authority_controlled/src/task_runner.cpp
  )
  target_include_directories(
    muesli_bt_controlled_authority_runner
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/experiments/invocation_authority_controlled/src
  )
  target_link_libraries(
    muesli_bt_controlled_authority_runner
    PUBLIC muesli_bt_controlled_authority_variants muesli_bt_core
  )
  target_compile_features(muesli_bt_controlled_authority_runner PUBLIC cxx_std_20)
  if (MSVC)
    target_compile_options(muesli_bt_controlled_authority_runner PRIVATE /W4 /permissive-)
  else ()
    target_compile_options(muesli_bt_controlled_authority_runner PRIVATE -Wall -Wextra -Wpedantic)
  endif ()

  add_executable(
    muesli_bt_controlled_authority_runner_tests
    experiments/invocation_authority_controlled/tests/task_runner_tests.cpp
  )
  target_link_libraries(
    muesli_bt_controlled_authority_runner_tests
    PRIVATE muesli_bt_controlled_authority_runner
  )
  target_compile_definitions(
    muesli_bt_controlled_authority_runner_tests
    PRIVATE
      MUESLI_BT_CONTROLLED_AUTHORITY_COMMON_TREE="${CMAKE_CURRENT_SOURCE_DIR}/experiments/invocation_authority_controlled/lisp/common_task.lisp"
  )
  add_test(
    NAME muesli_bt_controlled_authority_runner
    COMMAND muesli_bt_controlled_authority_runner_tests
  )

  add_library(
    muesli_bt_controlled_authority_campaign_engine
    experiments/invocation_authority_controlled/src/campaign_engine.cpp
    experiments/invocation_authority_controlled/src/campaign_outcome.cpp
    experiments/invocation_authority_controlled/src/campaign_plan.cpp
    experiments/invocation_authority_controlled/src/campaign_writer.cpp
    experiments/invocation_authority_controlled/src/scripted_provider.cpp
  )
  target_include_directories(
    muesli_bt_controlled_authority_campaign_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/experiments/invocation_authority_controlled/src
  )
  target_link_libraries(
    muesli_bt_controlled_authority_campaign_engine
    PUBLIC muesli_bt_controlled_authority_runner
  )
  target_compile_features(muesli_bt_controlled_authority_campaign_engine PUBLIC cxx_std_20)
  if (MSVC)
    target_compile_options(
      muesli_bt_controlled_authority_campaign_engine PRIVATE /W4 /permissive-
    )
  else ()
    target_compile_options(
      muesli_bt_controlled_authority_campaign_engine PRIVATE -Wall -Wextra -Wpedantic
    )
  endif ()

  add_executable(
    muesli_bt_controlled_authority_campaign
    experiments/invocation_authority_controlled/src/campaign_main.cpp
  )
  target_link_libraries(
    muesli_bt_controlled_authority_campaign
    PRIVATE muesli_bt_controlled_authority_campaign_engine
  )

  add_library(
    muesli_bt_controlled_authority_timing_engine
    experiments/invocation_authority_controlled/src/timing_engine.cpp
    experiments/invocation_authority_controlled/src/timing_plan.cpp
  )
  target_include_directories(
    muesli_bt_controlled_authority_timing_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/experiments/invocation_authority_controlled/src
  )
  target_link_libraries(
    muesli_bt_controlled_authority_timing_engine
    PUBLIC muesli_bt_controlled_authority_runner
  )
  target_compile_features(muesli_bt_controlled_authority_timing_engine PUBLIC cxx_std_20)
  if (MSVC)
    target_compile_options(
      muesli_bt_controlled_authority_timing_engine PRIVATE /W4 /permissive-
    )
  else ()
    target_compile_options(
      muesli_bt_controlled_authority_timing_engine PRIVATE -Wall -Wextra -Wpedantic
    )
  endif ()

  add_executable(
    muesli_bt_controlled_authority_timing
    experiments/invocation_authority_controlled/src/timing_main.cpp
  )
  target_link_libraries(
    muesli_bt_controlled_authority_timing
    PRIVATE muesli_bt_controlled_authority_timing_engine
  )
endif ()

add_executable(muslisp_tests tests/test_main.cpp src/repl_support.cpp)
if (MUESLI_BT_BUILD_INTEGRATION_PYBULLET)
  target_link_libraries(muslisp_tests PRIVATE muesli_bt_integration_pybullet)
  target_compile_definitions(muslisp_tests PRIVATE MUESLI_BT_WITH_PYBULLET_INTEGRATION=1)
else ()
  target_link_libraries(muslisp_tests PRIVATE muesli_bt_core)
  target_compile_definitions(muslisp_tests PRIVATE MUESLI_BT_WITH_PYBULLET_INTEGRATION=0)
endif ()

if (TARGET muesli_bt_integration_ros2)
  find_package(rosbag2_cpp REQUIRED)
  find_package(rosbag2_storage REQUIRED)
  target_link_libraries(muslisp_tests PRIVATE muesli_bt_integration_ros2)
  target_compile_definitions(muslisp_tests PRIVATE MUESLI_BT_WITH_ROS2_INTEGRATION=1)
else ()
  target_compile_definitions(muslisp_tests PRIVATE MUESLI_BT_WITH_ROS2_INTEGRATION=0)
endif ()

if (TARGET muesli_bt_integration_model_service)
  target_link_libraries(muslisp_tests PRIVATE muesli_bt_integration_model_service)
endif ()

add_test(NAME muslisp_tests COMMAND muslisp_tests)

if (TARGET muesli_bt_integration_booster AND UNIX)
  add_executable(muesli_bt_booster_bridge_tests tests/test_booster_bridge.cpp)
  target_link_libraries(muesli_bt_booster_bridge_tests PRIVATE muesli_bt_integration_booster)
  add_test(NAME muesli_bt_booster_bridge COMMAND muesli_bt_booster_bridge_tests)
  set_tests_properties(muesli_bt_booster_bridge PROPERTIES TIMEOUT 10)
endif ()

add_executable(muesli_bt_humanoid_vla_scenario_tests tests/test_humanoid_vla_scenarios.cpp)
if (TARGET muesli_bt_integration_model_service)
  target_link_libraries(muesli_bt_humanoid_vla_scenario_tests PRIVATE muesli_bt_integration_model_service)
else ()
  target_link_libraries(muesli_bt_humanoid_vla_scenario_tests PRIVATE muesli_bt_core)
endif ()

foreach (scenario IN ITEMS normal_acceptance moved_ball supersession late_completion
                           duplicate_completion branch_halt re_entry emergency_interruption)
  add_test(
    NAME muesli_bt_humanoid_vla_${scenario}
    COMMAND muesli_bt_humanoid_vla_scenario_tests ${scenario}
  )
endforeach ()

if (TARGET muesli_bt_integration_model_service)
  add_executable(muesli_bt_model_service_bridge_tests tests/test_model_service_bridge.cpp)
  target_link_libraries(muesli_bt_model_service_bridge_tests PRIVATE muesli_bt_integration_model_service)
  add_test(NAME muesli_bt_model_service_bridge_tests COMMAND muesli_bt_model_service_bridge_tests)
endif ()

add_executable(muesli_bt_conformance_tests tests/conformance/mock_backend.cpp
                                           tests/conformance/test_conformance_main.cpp)
target_link_libraries(muesli_bt_conformance_tests PRIVATE muesli_bt_core)
target_include_directories(muesli_bt_conformance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if (TARGET muesli_bt_integration_model_service)
  target_link_libraries(muesli_bt_conformance_tests PRIVATE muesli_bt_integration_model_service)
endif ()
if (TARGET muesli_bt_integration_ros2)
  target_link_libraries(muesli_bt_conformance_tests PRIVATE muesli_bt_integration_ros2)
  target_compile_definitions(muesli_bt_conformance_tests PRIVATE MUESLI_BT_WITH_ROS2_INTEGRATION=1)
else ()
  target_compile_definitions(muesli_bt_conformance_tests PRIVATE MUESLI_BT_WITH_ROS2_INTEGRATION=0)
endif ()

add_test(NAME muesli_bt_conformance_tests COMMAND muesli_bt_conformance_tests)

if (TARGET muesli_bt_integration_ros2)
  add_executable(muesli_bt_conformance_l2_rosbag_tests tests/conformance/test_conformance_l2_rosbag_main.cpp)
  target_link_libraries(
    muesli_bt_conformance_l2_rosbag_tests
    PRIVATE muesli_bt_core
            muesli_bt_integration_ros2
            rosbag2_cpp::rosbag2_cpp
            rosbag2_storage::rosbag2_storage
  )
  target_include_directories(muesli_bt_conformance_l2_rosbag_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(muesli_bt_conformance_l2_rosbag_tests PRIVATE MUESLI_BT_WITH_ROS2_INTEGRATION=1)
  add_test(NAME muesli_bt_conformance_l2_rosbag_tests COMMAND muesli_bt_conformance_l2_rosbag_tests)
  set_tests_properties(
    muesli_bt_conformance_l2_rosbag_tests
    PROPERTIES ENVIRONMENT "MUESLI_BT_L2_ARTIFACT_DIR=${CMAKE_BINARY_DIR}/ros2_l2_artifacts"
  )

  add_executable(nav2_capability_evidence tools/nav2_capability_evidence_main.cpp)
  target_link_libraries(nav2_capability_evidence PRIVATE muesli_bt_integration_ros2)
  target_include_directories(nav2_capability_evidence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(nav2_capability_evidence PRIVATE MUESLI_BT_WITH_ROS2_INTEGRATION=1)

  add_executable(
    wheeled_flagship_nav2_real_stack_evidence
    tools/wheeled_flagship_nav2_real_stack_evidence_main.cpp
  )
  target_link_libraries(wheeled_flagship_nav2_real_stack_evidence PRIVATE muesli_bt_integration_ros2)
  target_include_directories(wheeled_flagship_nav2_real_stack_evidence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(
    wheeled_flagship_nav2_real_stack_evidence
    PRIVATE MUESLI_BT_WITH_ROS2_INTEGRATION=1
  )
endif ()

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
  add_test(
    NAME muesli_bt_controlled_authority_c0
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_controlled_authority_c0.py"
  )
  add_test(
    NAME muesli_bt_controlled_authority_btcpp_protocol
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_controlled_authority_btcpp_protocol.py"
  )
  if (MUESLI_BT_BUILD_CONTROLLED_AUTHORITY_EXPERIMENT)
    add_test(
      NAME muesli_bt_controlled_authority_campaign
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_controlled_authority_campaign.py"
        "$<TARGET_FILE:muesli_bt_controlled_authority_campaign>"
    )
    add_test(
      NAME muesli_bt_controlled_authority_timing_campaign
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_controlled_authority_timing_campaign.py"
        "$<TARGET_FILE:muesli_bt_controlled_authority_timing>"
    )
  endif ()
  add_test(
    NAME muesli_bt_trace_validator_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_validate_trace.py"
  )
  add_test(
    NAME muesli_bt_flagship_compare_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_flagship_compare_runs.py"
  )
  add_test(
    NAME muesli_bt_flagship_same_robot_compare_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_flagship_same_robot_compare_runs.py"
  )
  add_test(
    NAME muesli_bt_generated_fragment_negative_fixtures
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_generated_bt_fragment.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dsl/generated-fragment-negative"
  )
  add_test(
    NAME muesli_bt_generated_guarded_recovery
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_generated_guarded_recovery.py"
  )
  add_test(
    NAME muesli_bt_humanoid_vla_evidence
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_humanoid_vla_evidence.py"
  )
  add_test(
    NAME muesli_bt_booster_studio_adapter
    COMMAND
      "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/examples/humanoid_model_mediated_approach/booster_studio/tests"
      -p "test_*.py" -v
  )
  if (TARGET muesli_bt_integration_booster AND UNIX)
    add_test(
      NAME muesli_bt_booster_bridge_runner
      COMMAND
        "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_booster_bridge_runner.py"
        --runner "$<TARGET_FILE:humanoid_model_mediated_trial>"
    )
    set_tests_properties(muesli_bt_booster_bridge_runner PROPERTIES TIMEOUT 15)
  endif ()
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import jsonschema"
    RESULT_VARIABLE MUESLI_BT_JSONSCHEMA_IMPORT_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if (MUESLI_BT_JSONSCHEMA_IMPORT_RESULT EQUAL 0)
    add_test(
      NAME muesli_bt_air_hockey_host
      COMMAND
        "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/examples/air_hockey_model_mediated_defence/host/tests"
        -p "test_*.py" -v
    )
    add_test(
      NAME muesli_bt_air_hockey_evidence
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_air_hockey_evidence.py"
    )
    add_test(
      NAME muesli_bt_humanoid_video_experiment
      COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/humanoid_model_mediated_approach/run_trials.py"
        --runner "$<TARGET_FILE:humanoid_model_mediated_trial>" --check
    )
    set_tests_properties(muesli_bt_humanoid_video_experiment PROPERTIES TIMEOUT 30)
    if (TARGET muesli_bt_air_hockey_scenario_tests)
      foreach (scenario IN ITEMS H1 H2a H2b H3 H4 H5 H6 H7 H8)
        string(TOLOWER "${scenario}" scenario_name)
        add_test(
          NAME muesli_bt_air_hockey_${scenario_name}
          COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/air_hockey_model_mediated_defence/run_g2.py"
            --runner "$<TARGET_FILE:muesli_bt_air_hockey_scenario_tests>"
            --scenario "${scenario}"
        )
        set_tests_properties(muesli_bt_air_hockey_${scenario_name} PROPERTIES TIMEOUT 30)
      endforeach ()
      add_test(
        NAME muesli_bt_air_hockey_wp7_native
        COMMAND
          "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/air_hockey_model_mediated_defence/run_wp7.py"
          check-native --runner "$<TARGET_FILE:muesli_bt_air_hockey_scenario_tests>"
      )
      set_tests_properties(muesli_bt_air_hockey_wp7_native PROPERTIES TIMEOUT 30)
    endif ()
  else ()
    message(
      STATUS
      "jsonschema not found; air-hockey and humanoid video experiment CTests are not registered"
    )
  endif ()
  add_test(
    NAME muesli_bt_wheeled_flagship_nav_capability_evidence
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_wheeled_flagship_nav_capability_evidence.py"
            --muslisp "$<TARGET_FILE:muslisp>" --check
  )
  add_test(
    NAME muesli_bt_wheeled_flagship_nav2_real_stack_evidence_metadata
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_wheeled_flagship_nav2_real_stack_evidence.py"
            --check
  )
  if (TARGET nav2_capability_evidence)
    add_test(
      NAME muesli_bt_nav2_capability_evidence
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_nav2_capability_evidence.py"
              --helper "$<TARGET_FILE:nav2_capability_evidence>" --check
    )
  endif ()
  add_test(
    NAME muesli_bt_bt_node_option_docs
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-bt-node-option-docs.py"
  )
  add_test(
    NAME muesli_bt_docs_user_prose
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-docs-user-prose.py"
  )
  add_test(
    NAME muesli_bt_docs_snippet_freshness
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_docs_snippet_freshness.py"
  )
  add_test(
    NAME muesli_bt_reference_coverage
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-reference-coverage.py"
  )
endif ()

if (MUESLI_BT_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif ()

install(
  TARGETS muesli_bt_core
  EXPORT muesli_btTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (MUESLI_BT_BUILD_INTEGRATION_PYBULLET)
  install(
    TARGETS muesli_bt_integration_pybullet
    EXPORT muesli_btTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif ()

if (TARGET muesli_bt_integration_booster)
  install(
    TARGETS muesli_bt_integration_booster
    EXPORT muesli_btTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif ()

if (TARGET muesli_bt_integration_webots)
  install(
    TARGETS muesli_bt_integration_webots
    EXPORT muesli_btTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif ()

if (TARGET muesli_bt_integration_ros2)
  install(
    TARGETS muesli_bt_integration_ros2
    EXPORT muesli_btTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  install(TARGETS muslisp_ros2 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()

if (TARGET muesli_bt_integration_model_service)
  install(
    TARGETS muesli_bt_integration_model_service
    EXPORT muesli_btTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif ()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (MUESLI_BT_BUILD_INTEGRATION_PYBULLET)
  install(
    DIRECTORY integrations/pybullet/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pybullet
    FILES_MATCHING
    PATTERN "*.hpp"
  )
endif ()

if (TARGET muesli_bt_integration_booster)
  install(
    DIRECTORY integrations/booster/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/booster
    FILES_MATCHING
    PATTERN "*.hpp"
  )
endif ()

if (TARGET muesli_bt_integration_webots)
  install(
    DIRECTORY integrations/webots/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/webots
    FILES_MATCHING
    PATTERN "*.hpp"
  )
endif ()

if (TARGET muesli_bt_integration_ros2)
  install(
    DIRECTORY integrations/ros2/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ros2
    FILES_MATCHING
    PATTERN "*.hpp"
  )
endif ()

install(
  FILES docs/contracts/muesli-studio-integration.md docs/contracts/runtime-contract-v1.md
        docs/contracts/compatibility.md docs/contracts/conformance.md
  DESTINATION ${MUESLI_BT_INSTALL_SHAREDIR}/contracts
)

install(FILES schemas/event_log/v1/mbt.evt.v1.schema.json
        DESTINATION ${MUESLI_BT_INSTALL_SHAREDIR}/schemas/event_log/v1
)

install(
  DIRECTORY schemas/humanoid_booster/
  DESTINATION ${MUESLI_BT_INSTALL_SHAREDIR}/schemas/humanoid_booster
)

install(
  DIRECTORY schemas/bt_node_options/
  DESTINATION ${MUESLI_BT_INSTALL_SHAREDIR}/schemas/bt_node_options
)

install(
  EXPORT muesli_btTargets
  NAMESPACE muesli_bt::
  FILE muesli_btTargets.cmake
  DESTINATION ${MUESLI_BT_INSTALL_CMAKEDIR}
)

set(MUESLI_BT_PACKAGE_HAS_ROS2_INTEGRATION_TARGET FALSE)
if (TARGET muesli_bt_integration_ros2)
  set(MUESLI_BT_PACKAGE_HAS_ROS2_INTEGRATION_TARGET TRUE)
endif ()

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/muesli_btConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/muesli_btConfig.cmake
  INSTALL_DESTINATION ${MUESLI_BT_INSTALL_CMAKEDIR}
  PATH_VARS MUESLI_BT_INSTALL_SHAREDIR
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/muesli_btConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/muesli_btConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/muesli_btConfigVersion.cmake
        DESTINATION ${MUESLI_BT_INSTALL_CMAKEDIR}
)
