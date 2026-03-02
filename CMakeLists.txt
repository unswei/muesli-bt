cmake_minimum_required(VERSION 3.20)

project(
  muesli_bt
  VERSION 0.1.0
  LANGUAGES CXX
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

add_library(
  muesli_bt_core
  src/bt/blackboard.cpp
  src/bt/compiler.cpp
  src/bt/event_log.cpp
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

target_include_directories(
  muesli_bt_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(muesli_bt_core PROPERTIES EXPORT_NAME runtime)

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
  set_target_properties(muesli_bt_integration_pybullet PROPERTIES EXPORT_NAME integration_pybullet)
  target_compile_features(muesli_bt_integration_pybullet PUBLIC cxx_std_20)
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
  set_target_properties(muesli_bt_integration_webots PROPERTIES EXPORT_NAME integration_webots)
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

add_executable(muslisp src/main.cpp)
target_link_libraries(muslisp PRIVATE muesli_bt_core)

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
  find_package(pybind11 CONFIG QUIET)

  if (NOT pybind11_FOUND)
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
      OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE PYBIND11_DIR_RESULT
    )
    if (PYBIND11_DIR_RESULT EQUAL 0 AND EXISTS "${PYBIND11_CMAKE_DIR}")
      list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_CMAKE_DIR}")
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

add_executable(muslisp_tests tests/test_main.cpp)
if (MUESLI_BT_BUILD_INTEGRATION_PYBULLET)
  target_link_libraries(muslisp_tests PRIVATE muesli_bt_integration_pybullet)
  target_compile_definitions(muslisp_tests PRIVATE MUESLI_BT_WITH_PYBULLET_INTEGRATION=1)
else ()
  target_link_libraries(muslisp_tests PRIVATE muesli_bt_core)
  target_compile_definitions(muslisp_tests PRIVATE MUESLI_BT_WITH_PYBULLET_INTEGRATION=0)
endif ()

add_test(NAME muslisp_tests COMMAND muslisp_tests)

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

if (TARGET muesli_bt_integration_webots)
  install(
    TARGETS muesli_bt_integration_webots
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

if (TARGET muesli_bt_integration_webots)
  install(
    DIRECTORY integrations/webots/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/webots
    FILES_MATCHING
    PATTERN "*.hpp"
  )
endif ()

install(FILES docs/contracts/muesli-studio-integration.md
        DESTINATION ${MUESLI_BT_INSTALL_SHAREDIR}/contracts
)

install(FILES schema/mbt.evt.v1.schema.json DESTINATION ${MUESLI_BT_INSTALL_SHAREDIR}/schema)

install(
  EXPORT muesli_btTargets
  NAMESPACE muesli_bt::
  FILE muesli_btTargets.cmake
  DESTINATION ${MUESLI_BT_INSTALL_CMAKEDIR}
)

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
