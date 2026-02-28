#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${ROOT_DIR}/.venv-py311"
PYTHON_BIN="${1:-${VENV_DIR}/bin/python}"
LAST_CMD=""

print_diagnostics() {
  echo "---- demo-setup diagnostics ----" >&2
  echo "python executable: ${PYTHON_BIN}" >&2
  if [[ -x "${PYTHON_BIN}" ]]; then
    "${PYTHON_BIN}" - <<'PY' >&2 || true
import platform
import sys
print("python version:", sys.version.replace("\n", " "))
print("architecture:", platform.machine())
try:
    import pybind11
    print("pybind11 cmake dir:", pybind11.get_cmake_dir())
except Exception as exc:
    print("pybind11 cmake dir: <unavailable>", exc)
PY
  else
    echo "python version: <unavailable>" >&2
    echo "architecture: $(uname -m)" >&2
    echo "pybind11 cmake dir: <unavailable>" >&2
  fi

  if [[ -f "${ROOT_DIR}/build/dev/CMakeCache.txt" ]]; then
    local generator
    generator="$(grep -E '^CMAKE_GENERATOR:' "${ROOT_DIR}/build/dev/CMakeCache.txt" | head -n1 | cut -d= -f2- || true)"
    if [[ -n "${generator}" ]]; then
      echo "cmake generator: ${generator}" >&2
    else
      echo "cmake generator: <unknown>" >&2
    fi
  else
    echo "cmake generator: Ninja (from dev preset)" >&2
  fi

  echo "exact failing command: ${LAST_CMD}" >&2
  echo "--------------------------------" >&2
}

on_error() {
  local rc="$?"
  print_diagnostics
  exit "${rc}"
}
trap on_error ERR

run_cmd() {
  LAST_CMD="$*"
  echo "+ $*"
  eval "$@"
}

ensure_venv() {
  if [[ -x "${PYTHON_BIN}" ]]; then
    return
  fi

  if command -v uv >/dev/null 2>&1; then
    run_cmd "uv venv --python 3.11 \"${VENV_DIR}\""
  else
    local python311
    python311="$(command -v python3.11 || true)"
    if [[ -z "${python311}" ]]; then
      echo "error: python3.11 was not found and uv is unavailable." >&2
      exit 1
    fi
    run_cmd "\"${python311}\" -m venv \"${VENV_DIR}\""
  fi
  PYTHON_BIN="${VENV_DIR}/bin/python"
}

main() {
  ensure_venv

  run_cmd "\"${PYTHON_BIN}\" -m pip install --upgrade pip"
  run_cmd "\"${PYTHON_BIN}\" -m pip install -r \"${ROOT_DIR}/examples/pybullet_racecar/requirements.txt\""

  run_cmd "cmake --preset dev \
    -DMUESLI_BT_BUILD_PYTHON_BRIDGE=ON \
    -DPython3_EXECUTABLE=\"${PYTHON_BIN}\" \
    -DPython3_FIND_VIRTUALENV=ONLY \
    -DPython3_FIND_STRATEGY=LOCATION"
  run_cmd "cmake --build --preset dev -j"

  LAST_CMD="verify bridge output"
  if [[ ! -d "${ROOT_DIR}/build/dev/python" ]]; then
    echo "error: expected bridge output directory missing: ${ROOT_DIR}/build/dev/python" >&2
    exit 1
  fi
  if ! ls "${ROOT_DIR}/build/dev/python"/muesli_bt_bridge*.so >/dev/null 2>&1 && \
     ! ls "${ROOT_DIR}/build/dev/python"/muesli_bt_bridge*.dylib >/dev/null 2>&1; then
    echo "error: bridge module was not produced in ${ROOT_DIR}/build/dev/python" >&2
    exit 1
  fi

  echo "demo-setup complete."
  echo "Run with: make demo-run MODE=bt_planner"
}

main "$@"
