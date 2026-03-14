#!/usr/bin/env bash
set -euo pipefail

: "${ISAACSIM_VERSION:?ISAACSIM_VERSION must be set}"
: "${ISAACLAB_ROOT:?ISAACLAB_ROOT must be set}"
: "${ISAACLAB_RUNTIME_ROOT:?ISAACLAB_RUNTIME_ROOT must be set}"
: "${ISAACLAB_VENV:?ISAACLAB_VENV must be set}"

install_args=()
if [[ $# -gt 0 ]]; then
  install_args=("$@")
else
  install_args=("${ISAACLAB_FRAMEWORKS:-none}")
fi

mkdir -p "${ISAACLAB_RUNTIME_ROOT}"

if [[ ! -x "${ISAACLAB_VENV}/bin/python" ]]; then
  uv venv --python 3.11 "${ISAACLAB_VENV}"
fi

if ! "${ISAACLAB_VENV}/bin/python" -m pip --version >/dev/null 2>&1; then
  "${ISAACLAB_VENV}/bin/python" -m ensurepip --upgrade
fi

# shellcheck disable=SC1091
source "${ISAACLAB_VENV}/bin/activate"
set +u
# ROS setup scripts are not nounset-safe.
source /opt/ros/humble/setup.bash
set -u

export OMNI_KIT_ACCEPT_EULA="${OMNI_KIT_ACCEPT_EULA:-YES}"
export ACCEPT_EULA="${ACCEPT_EULA:-Y}"
export PRIVACY_CONSENT="${PRIVACY_CONSENT:-Y}"
export PIP_DISABLE_PIP_VERSION_CHECK=1

# Isaac Lab currently pulls flatdict==4.0.1, whose build still imports pkg_resources.
python -m pip install --upgrade pip "setuptools<81" wheel toml

# Build isolation would otherwise pull a newer setuptools into a temporary env,
# which breaks flatdict's legacy pkg_resources import during Isaac Lab install.
if ! python -c "import flatdict" >/dev/null 2>&1; then
  python -m pip install --no-build-isolation "flatdict==4.0.1"
fi

if ! python -c "import isaacsim" >/dev/null 2>&1; then
  python -m pip install \
    "isaacsim[all,extscache]==${ISAACSIM_VERSION}" \
    --extra-index-url https://pypi.nvidia.com
fi

arch=$(uname -m)
if [[ "${arch}" == "aarch64" || "${arch}" == "arm64" ]]; then
  torch_index_url="https://download.pytorch.org/whl/cu130"
  torch_version="2.9.0"
  torchvision_version="0.24.0"
else
  torch_index_url="https://download.pytorch.org/whl/cu128"
  torch_version="2.7.0"
  torchvision_version="0.22.0"
fi

if python - <<'PY'
import sys

target_torch = sys.argv[1]
target_torchvision = sys.argv[2]

try:
    import torch
    import torchvision
except ImportError:
    raise SystemExit(1)

installed_torch = torch.__version__.split("+", 1)[0]
installed_torchvision = torchvision.__version__.split("+", 1)[0]

raise SystemExit(
    0 if installed_torch == target_torch and installed_torchvision == target_torchvision else 1
)
PY
"${torch_version}" "${torchvision_version}"; then
  echo "Torch ${torch_version} and torchvision ${torchvision_version} already match the target build; skipping reinstall."
else
  python -m pip install -U \
    --index-url "${torch_index_url}" \
    "torch==${torch_version}" \
    "torchvision==${torchvision_version}"
fi

cd "${ISAACLAB_ROOT}"
"${ISAACLAB_ROOT}/isaaclab.sh" --install "${install_args[@]}"
"${ISAACLAB_ROOT}/isaaclab.sh" -p -m pip uninstall -y quadprog || true

cat <<EOF
Isaac Sim ${ISAACSIM_VERSION} and Isaac Lab are installed in:
  ${ISAACLAB_VENV}

Run Isaac Lab commands with:
  isaaclab-python -c "import isaacsim, isaaclab; print('ok')"
  enter-isaaclab
EOF
