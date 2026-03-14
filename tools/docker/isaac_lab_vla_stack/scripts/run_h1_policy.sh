#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_ROOT=${ISAACSIM_ROS_WORKSPACES_ROOT:-/workspace/ros-workspaces/IsaacSim-ros_workspaces}
ROS_DISTRO_NAME=${ROS_DISTRO:-humble}
ROS_WS_DIR="${WORKSPACE_ROOT}/${ROS_DISTRO_NAME}_ws"
NAMESPACE=${H1_POLICY_NAMESPACE:-h1_01}
USE_NAMESPACE=${H1_POLICY_USE_NAMESPACE:-True}
PUBLISH_PERIOD_MS=${H1_POLICY_PUBLISH_PERIOD_MS:-5}
USE_SIM_TIME=${H1_POLICY_USE_SIM_TIME:-True}

if [[ ! -f "${ROS_WS_DIR}/install/setup.bash" ]]; then
  /tmp/stack-scripts/bootstrap_h1_policy_workspace.sh
fi

set +u
source "/opt/ros/${ROS_DISTRO_NAME}/setup.bash"
source "${ROS_WS_DIR}/install/setup.bash"
set -u

exec ros2 launch h1_fullbody_controller h1_fullbody_controller.launch.py \
  namespace:="${NAMESPACE#/}" \
  use_namespace:="${USE_NAMESPACE}" \
  publish_period_ms:="${PUBLISH_PERIOD_MS}" \
  use_sim_time:="${USE_SIM_TIME}" \
  "$@"
