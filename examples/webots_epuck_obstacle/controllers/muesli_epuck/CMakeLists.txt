cmake_minimum_required(VERSION 3.20)

project(muesli_webots_epuck_controller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(MUESLI_BT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

if(NOT TARGET muesli_bt_core)
  add_subdirectory("${MUESLI_BT_ROOT}" "${CMAKE_BINARY_DIR}/muesli_bt_root_build")
endif()

find_path(WEBOTS_CPP_INCLUDE_DIR webots/Robot.hpp
  HINTS ENV WEBOTS_HOME "${WEBOTS_HOME}"
  PATH_SUFFIXES include/controller/cpp Contents/include/controller/cpp)

find_library(WEBOTS_CONTROLLER_LIBRARY Controller
  HINTS ENV WEBOTS_HOME "${WEBOTS_HOME}"
  PATH_SUFFIXES lib/controller Contents/lib/controller)
find_library(WEBOTS_CPP_CONTROLLER_LIBRARY CppController
  HINTS ENV WEBOTS_HOME "${WEBOTS_HOME}"
  PATH_SUFFIXES lib/controller Contents/lib/controller)

if(NOT WEBOTS_CPP_INCLUDE_DIR OR NOT WEBOTS_CONTROLLER_LIBRARY OR NOT WEBOTS_CPP_CONTROLLER_LIBRARY)
  message(FATAL_ERROR "Webots C++ controller SDK not found. Set WEBOTS_HOME to your Webots install path.")
endif()

add_executable(muesli_epuck muesli_epuck.cpp)
target_link_libraries(muesli_epuck PRIVATE muesli_bt_core ${WEBOTS_CPP_CONTROLLER_LIBRARY} ${WEBOTS_CONTROLLER_LIBRARY})
target_include_directories(muesli_epuck PRIVATE ${WEBOTS_CPP_INCLUDE_DIR} "${MUESLI_BT_ROOT}/include")
set_target_properties(muesli_epuck PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
