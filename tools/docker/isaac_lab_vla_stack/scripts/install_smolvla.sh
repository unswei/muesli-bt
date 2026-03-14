#!/usr/bin/env bash
set -euo pipefail

: "${LEROBOT_REF:?LEROBOT_REF must be set}"
: "${LEROBOT_ROOT:?LEROBOT_ROOT must be set}"
: "${SMOLVLA_VENV:?SMOLVLA_VENV must be set}"
: "${SMOLVLA_PYTHON_VERSION:?SMOLVLA_PYTHON_VERSION must be set}"

mkdir -p "$(dirname -- "${LEROBOT_ROOT}")" "$(dirname -- "${SMOLVLA_VENV}")"

git clone --depth 1 --branch "${LEROBOT_REF}" https://github.com/huggingface/lerobot.git "${LEROBOT_ROOT}"

uv venv --python "${SMOLVLA_PYTHON_VERSION}" "${SMOLVLA_VENV}"
uv pip install --python "${SMOLVLA_VENV}/bin/python" --upgrade pip setuptools wheel
uv pip install --python "${SMOLVLA_VENV}/bin/python" -e "${LEROBOT_ROOT}[smolvla,async]"
