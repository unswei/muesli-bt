#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  cat >&2 <<'EOF'
Usage:
  isaac-sim-state status
  isaac-sim-state play
  isaac-sim-state pause
  isaac-sim-state stop
  isaac-sim-state quit
  isaac-sim-state reset
  isaac-sim-state step <count>
EOF
  exit 1
fi

set +u
source /opt/ros/humble/setup.bash
set -u

case "$1" in
  status)
    exec ros2 service call /get_simulation_state simulation_interfaces/srv/GetSimulationState
    ;;
  play)
    exec ros2 service call /set_simulation_state simulation_interfaces/srv/SetSimulationState "{state: {state: 1}}"
    ;;
  pause)
    exec ros2 service call /set_simulation_state simulation_interfaces/srv/SetSimulationState "{state: {state: 2}}"
    ;;
  stop)
    exec ros2 service call /set_simulation_state simulation_interfaces/srv/SetSimulationState "{state: {state: 0}}"
    ;;
  quit)
    exec ros2 service call /set_simulation_state simulation_interfaces/srv/SetSimulationState "{state: {state: 3}}"
    ;;
  reset)
    exec ros2 service call /reset_simulation simulation_interfaces/srv/ResetSimulation
    ;;
  step)
    if [[ $# -ne 2 ]]; then
      echo "isaac-sim-state step requires a frame count" >&2
      exit 1
    fi
    exec ros2 service call /step_simulation simulation_interfaces/srv/StepSimulation "{steps: $2}"
    ;;
  *)
    echo "Unsupported simulation state command: $1" >&2
    exit 1
    ;;
esac
