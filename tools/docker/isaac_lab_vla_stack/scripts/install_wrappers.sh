#!/usr/bin/env bash
set -euo pipefail

: "${ISAACLAB_ROOT:?ISAACLAB_ROOT must be set}"
: "${ISAACLAB_RUNTIME_ROOT:?ISAACLAB_RUNTIME_ROOT must be set}"
: "${ISAACLAB_VENV:?ISAACLAB_VENV must be set}"
: "${SMOLVLA_VENV:?SMOLVLA_VENV must be set}"
: "${OPENPI_VENV:?OPENPI_VENV must be set}"
: "${OPENPI_ROOT:?OPENPI_ROOT must be set}"

install -d \
  /usr/local/bin \
  /workspace \
  /workspace/muesli-bt \
  "${ISAACLAB_RUNTIME_ROOT}" \
  /root/.cache/ov \
  /root/.cache/pip \
  /root/.cache/uv \
  /root/.cache/huggingface \
  /root/.cache/nvidia/GLCache \
  /root/.nv/ComputeCache \
  /root/.nvidia-omniverse/logs \
  /root/.local/share/ov/data \
  /root/Documents

cat > /usr/local/bin/with-isaaclab-env <<EOF
#!/usr/bin/env bash
set -euo pipefail
if [[ ! -x "${ISAACLAB_VENV}/bin/python" ]]; then
  echo "Isaac Sim / Isaac Lab runtime is not installed yet." >&2
  echo "Run: install-isaacsim" >&2
  exit 1
fi
set +u
source /opt/ros/humble/setup.bash
set -u
source "${ISAACLAB_VENV}/bin/activate"
export OMNI_KIT_ACCEPT_EULA="\${OMNI_KIT_ACCEPT_EULA:-YES}"
exec "\$@"
EOF

cat > /usr/local/bin/install-isaacsim <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec /tmp/stack-scripts/install_isaacsim.sh "\$@"
EOF

cat > /usr/local/bin/isaaclab-python <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec with-isaaclab-env "${ISAACLAB_ROOT}/isaaclab.sh" -p "\$@"
EOF

cat > /usr/local/bin/enter-isaaclab <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec with-isaaclab-env /bin/bash -i
EOF

cat > /usr/local/bin/smolvla-python <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec "${SMOLVLA_VENV}/bin/python" "\$@"
EOF

cat > /usr/local/bin/smolvla-policy-server <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec "${SMOLVLA_VENV}/bin/python" -m lerobot.async_inference.policy_server "\$@"
EOF

cat > /usr/local/bin/openpi-python <<EOF
#!/usr/bin/env bash
set -euo pipefail
exec "${OPENPI_VENV}/bin/python" "\$@"
EOF

cat > /usr/local/bin/openpi-serve-policy <<EOF
#!/usr/bin/env bash
set -euo pipefail
cd "${OPENPI_ROOT}"
exec "${OPENPI_VENV}/bin/python" scripts/serve_policy.py "\$@"
EOF

cat > /usr/local/bin/enter-smolvla <<EOF
#!/usr/bin/env bash
set -euo pipefail
set +u
source /opt/ros/humble/setup.bash
set -u
source "${SMOLVLA_VENV}/bin/activate"
cd /workspace/muesli-bt
exec /bin/bash -i
EOF

cat > /usr/local/bin/enter-openpi <<EOF
#!/usr/bin/env bash
set -euo pipefail
set +u
source /opt/ros/humble/setup.bash
set -u
source "${OPENPI_VENV}/bin/activate"
cd /workspace/muesli-bt
exec /bin/bash -i
EOF

chmod +x \
  /usr/local/bin/with-isaaclab-env \
  /usr/local/bin/install-isaacsim \
  /usr/local/bin/isaaclab-python \
  /usr/local/bin/enter-isaaclab \
  /usr/local/bin/smolvla-python \
  /usr/local/bin/smolvla-policy-server \
  /usr/local/bin/openpi-python \
  /usr/local/bin/openpi-serve-policy \
  /usr/local/bin/enter-smolvla \
  /usr/local/bin/enter-openpi

cat >> /root/.bashrc <<EOF

# muesli-bt Isaac Lab + VLA stack
source /opt/ros/humble/setup.bash
export ISAACLAB_ROOT="${ISAACLAB_ROOT}"
export ISAACLAB_VENV="${ISAACLAB_VENV}"
export OPENPI_ROOT="${OPENPI_ROOT}"
alias isaaclab="${ISAACLAB_ROOT}/isaaclab.sh"
alias isaac-python="isaaclab-python"
alias isaac-install="install-isaacsim"
alias isaac-shell="enter-isaaclab"
alias smolvla-server="smolvla-policy-server"
alias openpi-server="openpi-serve-policy"
alias smolvla-shell="enter-smolvla"
alias openpi-shell="enter-openpi"
EOF
