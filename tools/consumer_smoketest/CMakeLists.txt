cmake_minimum_required(VERSION 3.20)
project(muesli_bt_consumer_smoketest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(muesli_bt CONFIG REQUIRED)

if(NOT DEFINED muesli_bt_SHARE_DIR)
  message(FATAL_ERROR "muesli_bt_SHARE_DIR must be defined by muesli_btConfig.cmake")
endif()

if(NOT EXISTS "${muesli_bt_SHARE_DIR}/contracts/muesli-studio-integration.md")
  message(FATAL_ERROR "missing installed contract: ${muesli_bt_SHARE_DIR}/contracts/muesli-studio-integration.md")
endif()

if(NOT EXISTS "${muesli_bt_SHARE_DIR}/schemas/event_log/v1/mbt.evt.v1.schema.json")
  message(FATAL_ERROR "missing installed schema: ${muesli_bt_SHARE_DIR}/schemas/event_log/v1/mbt.evt.v1.schema.json")
endif()

if(NOT EXISTS "${muesli_bt_SHARE_DIR}/schemas/bt_node_options/v1/plan-action.schema.json")
  message(FATAL_ERROR "missing installed BT node option schema: ${muesli_bt_SHARE_DIR}/schemas/bt_node_options/v1/plan-action.schema.json")
endif()

add_executable(mbt_minimal_embedding_smoketest minimal_embedding_main.cpp)
target_link_libraries(mbt_minimal_embedding_smoketest PRIVATE muesli_bt::runtime)

if(TARGET muesli_bt::integration_pybullet)
  add_executable(mbt_inspector_smoketest main.cpp)
  target_link_libraries(mbt_inspector_smoketest PRIVATE muesli_bt::runtime muesli_bt::integration_pybullet)
endif()

if(TARGET muesli_bt::integration_webots)
  add_executable(mbt_inspector_webots_smoketest webots_main.cpp)
  target_link_libraries(mbt_inspector_webots_smoketest PRIVATE muesli_bt::runtime muesli_bt::integration_webots)
endif()

if(TARGET muesli_bt::integration_ros2)
  add_executable(mbt_inspector_ros2_smoketest ros2_main.cpp)
  target_link_libraries(mbt_inspector_ros2_smoketest PRIVATE muesli_bt::runtime muesli_bt::integration_ros2)
endif()
