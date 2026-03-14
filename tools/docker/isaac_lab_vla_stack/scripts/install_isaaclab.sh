#!/usr/bin/env bash
set -euo pipefail

: "${ISAACLAB_REF:?ISAACLAB_REF must be set}"
: "${ISAACLAB_ROOT:?ISAACLAB_ROOT must be set}"
: "${ISAACLAB_RUNTIME_ROOT:?ISAACLAB_RUNTIME_ROOT must be set}"

mkdir -p "$(dirname -- "${ISAACLAB_ROOT}")" "${ISAACLAB_RUNTIME_ROOT}"

git clone --depth 1 --branch "${ISAACLAB_REF}" https://github.com/isaac-sim/IsaacLab.git "${ISAACLAB_ROOT}"
chmod +x "${ISAACLAB_ROOT}/isaaclab.sh"
