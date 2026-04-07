#!/usr/bin/env bash
set -euo pipefail

TOPIC_NS=${ISAAC_WHEELED_TOPIC_NAMESPACE:-/robot}
TIMEOUT_S=${ISAAC_WHEELED_TOPIC_TIMEOUT_S:-30}
REQUIRED_TOPICS=(
  "${TOPIC_NS}/cmd_vel"
  "${TOPIC_NS}/odom"
)

set +u
source /opt/ros/humble/setup.bash
set -u

deadline=$((SECONDS + TIMEOUT_S))
while (( SECONDS < deadline )); do
  topic_list=$(ros2 topic list 2>/dev/null || true)
  missing=()
  for topic in "${REQUIRED_TOPICS[@]}"; do
    if ! grep -Fxq "${topic}" <<<"${topic_list}"; then
      missing+=("${topic}")
    fi
  done

  if (( ${#missing[@]} == 0 )); then
    echo "All Isaac wheeled showcase topics are present under ${TOPIC_NS}"
    exit 0
  fi

  sleep 1
done

echo "Timed out waiting for required Isaac wheeled showcase topics:" >&2
printf '  %s\n' "${missing[@]}" >&2
exit 1
