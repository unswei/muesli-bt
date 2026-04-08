#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
BUILD_DIR=${MUESLI_BT_ROS2_BUILD_DIR:-${REPO_ROOT}/build/linux-ros2}
LISP_SCRIPT=${MUESLI_BT_ROS2_SCRIPT:-${REPO_ROOT}/examples/repl_scripts/ros2-flagship-goal.lisp}
DISPLAY_NUM=${MUESLI_BT_DISPLAY_NUM:-88}
DISPLAY_VALUE=:${DISPLAY_NUM}
OUTPUT_DIR=${MUESLI_BT_CAPTURE_OUTPUT_DIR:-/home/deploy/isaac_tb3_live_ros2}

set +u
source /opt/ros/humble/setup.bash
set -u

if ! pgrep -af "Xvfb ${DISPLAY_VALUE}" >/dev/null 2>&1; then
  nohup Xvfb "${DISPLAY_VALUE}" -screen 0 1280x720x24 >/tmp/xvfb_tb3_live.log 2>&1 &
  sleep 2
fi

export DISPLAY="${DISPLAY_VALUE}"
export OMNI_KIT_ACCEPT_EULA=YES

if [[ ! -f "${BUILD_DIR}/muslisp_ros2" ]]; then
  cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
    -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
    -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
    -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
    -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
  cmake --build "${BUILD_DIR}" -j --target muslisp_ros2
fi

cleanup() {
  if [[ -n "${BRIDGE_PID:-}" ]]; then
    kill "${BRIDGE_PID}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${ISAAC_PID:-}" ]]; then
    kill "${ISAAC_PID}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

/usr/bin/env python3 "${SCRIPT_DIR}/ros2_udp_bridge.py" >/tmp/ros2_tb3_udp_bridge.log 2>&1 &
BRIDGE_PID=$!
sleep 1

source /home/deploy/env_isaaclab/bin/activate
python "${SCRIPT_DIR}/live_ros2_tb3_showcase.py" --output-dir "${OUTPUT_DIR}" >/tmp/live_ros2_tb3_showcase.log 2>&1 &
ISAAC_PID=$!
sleep 3

"${BUILD_DIR}/muslisp_ros2" "${LISP_SCRIPT}"
wait "${ISAAC_PID}"

echo "media: ${OUTPUT_DIR}/showcase.mp4"
echo "still: ${OUTPUT_DIR}/scene.png"
