#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

if [[ -f "${SCRIPT_DIR}/versions.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "${SCRIPT_DIR}/versions.env"
  set +a
fi

docker build \
  --pull \
  --file "${SCRIPT_DIR}/Dockerfile" \
  --build-arg "BASE_IMAGE=${BASE_IMAGE}" \
  --build-arg "BASE_TAG=${BASE_TAG}" \
  --build-arg "ISAACSIM_VERSION=${ISAACSIM_VERSION}" \
  --build-arg "ISAACLAB_REF=${ISAACLAB_REF}" \
  --build-arg "ISAACLAB_FRAMEWORKS=${ISAACLAB_FRAMEWORKS}" \
  --build-arg "ROS2_APT_PACKAGE=${ROS2_APT_PACKAGE}" \
  --build-arg "LEROBOT_REF=${LEROBOT_REF}" \
  --build-arg "OPENPI_REF=${OPENPI_REF}" \
  --build-arg "UV_VERSION=${UV_VERSION}" \
  --build-arg "SMOLVLA_PYTHON_VERSION=${SMOLVLA_PYTHON_VERSION}" \
  --build-arg "OPENPI_PYTHON_VERSION=${OPENPI_PYTHON_VERSION}" \
  --build-arg "ISAACLAB_ROOT=${ISAACLAB_ROOT}" \
  --build-arg "ISAACLAB_RUNTIME_ROOT=${ISAACLAB_RUNTIME_ROOT}" \
  --build-arg "ISAACLAB_VENV=${ISAACLAB_VENV}" \
  --build-arg "LEROBOT_ROOT=${LEROBOT_ROOT}" \
  --build-arg "OPENPI_ROOT=${OPENPI_ROOT}" \
  --build-arg "SMOLVLA_VENV=${SMOLVLA_VENV}" \
  --build-arg "OPENPI_VENV=${OPENPI_VENV}" \
  --tag "${IMAGE_NAME}:${IMAGE_TAG}" \
  "${SCRIPT_DIR}"
