#!/usr/bin/env bash
set -euo pipefail

TOPIC_NS=${H1_TOPIC_NAMESPACE:-/h1_01}
TIMEOUT_S=${H1_TOPIC_TIMEOUT_S:-30}
REQUIRED_TOPICS=(
  "${TOPIC_NS}/cmd_vel"
  "${TOPIC_NS}/odom"
  "${TOPIC_NS}/joint_states"
  "${TOPIC_NS}/joint_command"
  "${TOPIC_NS}/imu"
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
    echo "All H1 demo topics are present under ${TOPIC_NS}"
    exit 0
  fi

  sleep 1
done

echo "Timed out waiting for required H1 demo topics:" >&2
printf '  %s\n' "${missing[@]}" >&2
exit 1
