#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_ROOT=${ISAACSIM_ROS_WORKSPACES_ROOT:-/workspace/ros-workspaces/IsaacSim-ros_workspaces}
WORKSPACE_REF=${ISAACSIM_ROS_WORKSPACES_REF:-main}
ROS_DISTRO_NAME=${ROS_DISTRO:-humble}
ROS_WS_DIR="${WORKSPACE_ROOT}/${ROS_DISTRO_NAME}_ws"
REPO_URL=${ISAACSIM_ROS_WORKSPACES_REPO:-https://github.com/isaac-sim/IsaacSim-ros_workspaces.git}

mkdir -p "$(dirname "${WORKSPACE_ROOT}")"

if [[ ! -d "${WORKSPACE_ROOT}/.git" ]]; then
  git clone --depth 1 --branch "${WORKSPACE_REF}" "${REPO_URL}" "${WORKSPACE_ROOT}"
else
  git -C "${WORKSPACE_ROOT}" fetch --depth 1 origin "${WORKSPACE_REF}"
  git -C "${WORKSPACE_ROOT}" checkout --force FETCH_HEAD
fi

if [[ ! -d "${ROS_WS_DIR}/src/humanoid_locomotion_policy_example/h1_fullbody_controller" ]]; then
  echo "H1 policy package not found in ${ROS_WS_DIR}" >&2
  exit 1
fi

set +u
source "/opt/ros/${ROS_DISTRO_NAME}/setup.bash"
set -u

cd "${ROS_WS_DIR}"
colcon build --symlink-install --packages-up-to h1_fullbody_controller

echo "H1 policy workspace is ready at ${ROS_WS_DIR}"
