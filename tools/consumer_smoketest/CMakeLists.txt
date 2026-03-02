cmake_minimum_required(VERSION 3.20)
project(muesli_bt_consumer_smoketest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(muesli_bt CONFIG REQUIRED)

add_executable(mbt_inspector_smoketest main.cpp)
target_link_libraries(mbt_inspector_smoketest PRIVATE muesli_bt::runtime)
