ARG BASE_IMAGE=nvidia/cuda
ARG BASE_TAG=12.8.1-cudnn-runtime-ubuntu22.04
ARG UV_VERSION=0.5.1

FROM ghcr.io/astral-sh/uv:${UV_VERSION} AS uvbin
FROM ${BASE_IMAGE}:${BASE_TAG}
COPY --from=uvbin /uv /uvx /usr/local/bin/

SHELL ["/bin/bash", "-c"]

ARG ISAACSIM_VERSION=5.1.0
ARG ISAACLAB_REF=v2.3.1
ARG ISAACLAB_FRAMEWORKS=none
ARG ROS2_APT_PACKAGE=ros-base
ARG LEROBOT_REF=v0.4.1
ARG OPENPI_REF=main
ARG SMOLVLA_PYTHON_VERSION=3.11.9
ARG OPENPI_PYTHON_VERSION=3.11.9
ARG ISAACLAB_ROOT=/opt/robotics/IsaacLab
ARG ISAACLAB_RUNTIME_ROOT=/opt/robotics/runtime/isaaclab
ARG ISAACLAB_VENV=/opt/robotics/runtime/isaaclab/venv
ARG LEROBOT_ROOT=/opt/robotics/lerobot
ARG OPENPI_ROOT=/opt/robotics/openpi
ARG SMOLVLA_VENV=/opt/venvs/smolvla
ARG OPENPI_VENV=/opt/venvs/openpi

ENV LANG=C.UTF-8 \
    DEBIAN_FRONTEND=noninteractive \
    ACCEPT_EULA=Y \
    PRIVACY_CONSENT=Y \
    OMNI_KIT_ACCEPT_EULA=YES \
    ROS_DISTRO=humble \
    RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
    FASTRTPS_DEFAULT_PROFILES_FILE=/root/.ros/fastdds.xml \
    CYCLONEDDS_URI=file:///root/.ros/cyclonedds.xml \
    ROS_LOCALHOST_ONLY=0 \
    UV_LINK_MODE=copy \
    UV_PYTHON_DOWNLOADS=automatic \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    HF_HOME=/root/.cache/huggingface \
    ISAACSIM_VERSION=${ISAACSIM_VERSION} \
    ISAACLAB_ROOT=${ISAACLAB_ROOT} \
    ISAACLAB_RUNTIME_ROOT=${ISAACLAB_RUNTIME_ROOT} \
    ISAACLAB_VENV=${ISAACLAB_VENV} \
    LEROBOT_ROOT=${LEROBOT_ROOT} \
    OPENPI_ROOT=${OPENPI_ROOT} \
    SMOLVLA_VENV=${SMOLVLA_VENV} \
    OPENPI_VENV=${OPENPI_VENV} \
    ISAACLAB_REF=${ISAACLAB_REF} \
    ISAACLAB_FRAMEWORKS=${ISAACLAB_FRAMEWORKS} \
    ROS2_APT_PACKAGE=${ROS2_APT_PACKAGE} \
    LEROBOT_REF=${LEROBOT_REF} \
    OPENPI_REF=${OPENPI_REF} \
    SMOLVLA_PYTHON_VERSION=${SMOLVLA_PYTHON_VERSION} \
    OPENPI_PYTHON_VERSION=${OPENPI_PYTHON_VERSION}

USER root

COPY scripts/ /tmp/stack-scripts/

RUN chmod +x /tmp/stack-scripts/*.sh && \
    /tmp/stack-scripts/install_system_deps.sh

RUN /tmp/stack-scripts/install_isaaclab.sh
RUN /tmp/stack-scripts/install_ros2.sh
RUN /tmp/stack-scripts/install_smolvla.sh
RUN /tmp/stack-scripts/install_openpi.sh
RUN /tmp/stack-scripts/install_wrappers.sh

ENTRYPOINT ["/tmp/stack-scripts/entrypoint.sh"]
CMD ["bash"]
WORKDIR /workspace/muesli-bt
