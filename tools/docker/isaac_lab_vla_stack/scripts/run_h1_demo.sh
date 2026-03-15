#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=/workspace/muesli-bt
BUILD_DIR=${MUESLI_BT_ROS2_BUILD_DIR:-${REPO_ROOT}/build/linux-ros2}
BUILD_TYPE=${MUESLI_BT_ROS2_BUILD_TYPE:-Debug}
SCRIPT_PATH=${MUESLI_BT_H1_DEMO_SCRIPT:-${REPO_ROOT}/examples/isaac_h1_ros2_demo/lisp/main.lisp}

set +u
source /opt/ros/humble/setup.bash
set -u

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
    -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
    -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
    -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
    -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
fi

cmake --build "${BUILD_DIR}" -j --target muslisp_ros2

exec "${BUILD_DIR}/muslisp_ros2" "${SCRIPT_PATH}" "$@"
