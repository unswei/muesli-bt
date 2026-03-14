#!/usr/bin/env bash
set -euo pipefail

: "${ROS2_APT_PACKAGE:?ROS2_APT_PACKAGE must be set}"
: "${ISAACLAB_ROOT:?ISAACLAB_ROOT must be set}"

export DEBIAN_FRONTEND=noninteractive

add-apt-repository universe
curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu jammy main" \
  > /etc/apt/sources.list.d/ros2.list

apt-get update
apt-get install -y --no-install-recommends \
  "ros-humble-${ROS2_APT_PACKAGE}" \
  python3-colcon-common-extensions \
  python3-pip \
  ros-humble-rmw-cyclonedds-cpp \
  ros-humble-rmw-fastrtps-cpp \
  ros-humble-vision-msgs \
  ros-dev-tools

install -d /root/.ros
install -m 0644 /tmp/stack-scripts/ros/fastdds.xml /root/.ros/fastdds.xml
install -m 0644 /tmp/stack-scripts/ros/cyclonedds.xml /root/.ros/cyclonedds.xml

set +u
# ROS setup scripts are not nounset-safe.
source /opt/ros/humble/setup.bash
set -u
python3 -m pip install --no-cache-dir toml
python3 "${ISAACLAB_ROOT}/tools/install_deps.py" rosdep "${ISAACLAB_ROOT}/source"

apt-get clean
rm -rf /var/lib/apt/lists/*
