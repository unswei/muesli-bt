#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="${1:-${ROOT_DIR}/.venv-py311}"
PYTHON_VERSION="${PYTHON_VERSION:-3.11}"

if ! command -v uv >/dev/null 2>&1; then
  echo "error: uv is required (https://docs.astral.sh/uv/)" >&2
  exit 1
fi

echo "Creating virtual environment at ${VENV_DIR} using Python ${PYTHON_VERSION}"
uv venv --python "${PYTHON_VERSION}" "${VENV_DIR}"

echo "Installing docs/tooling requirements (excluding pybullet)"
REQ_NO_PYBULLET="$(mktemp)"
trap 'rm -f "${REQ_NO_PYBULLET}"' EXIT
grep -viE '^[[:space:]]*pybullet([<>=!~].*)?([[:space:]]*;.*)?$' "${ROOT_DIR}/docs/requirements.txt" > "${REQ_NO_PYBULLET}"
uv pip install --python "${VENV_DIR}/bin/python" -r "${REQ_NO_PYBULLET}"

if ! command -v dot >/dev/null 2>&1; then
  echo "warning: Graphviz 'dot' not found. Docs diagram rendering needs Graphviz." >&2
  echo "         Install with: brew install graphviz  (macOS) or sudo apt-get install graphviz" >&2
fi

echo "Installing numpy (required by pybullet at import time)"
uv pip install --python "${VENV_DIR}/bin/python" "numpy>=1.26"

echo "Installing pybullet"
if [[ "$(uname -s)" == "Darwin" ]]; then
  echo "Applying macOS build workaround: -Dfdopen=fdopen"
  CFLAGS='-Dfdopen=fdopen' CPPFLAGS='-Dfdopen=fdopen' \
    uv pip install --python "${VENV_DIR}/bin/python" pybullet
else
  uv pip install --python "${VENV_DIR}/bin/python" pybullet
fi

echo "Done."
echo "Activate with: source ${VENV_DIR}/bin/activate"
