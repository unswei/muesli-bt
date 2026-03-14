#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/../../.." && pwd)

if [[ -f "${SCRIPT_DIR}/versions.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "${SCRIPT_DIR}/versions.env"
  set +a
fi

if [[ $# -eq 0 ]]; then
  set -- bash
fi

CACHE_ROOT=${CACHE_ROOT:-"${HOME}/.cache/muesli-bt/isaac-lab-vla"}
CONTAINER_NAME=${CONTAINER_NAME:-muesli-bt-isaac-lab-vla}
DOCKER_GPU_MODE=${DOCKER_GPU_MODE:-auto}

mkdir -p \
  "${CACHE_ROOT}/ov-cache" \
  "${CACHE_ROOT}/pip-cache" \
  "${CACHE_ROOT}/uv-cache" \
  "${CACHE_ROOT}/hf-cache" \
  "${CACHE_ROOT}/ros-workspaces" \
  "${CACHE_ROOT}/gl-cache" \
  "${CACHE_ROOT}/compute-cache" \
  "${CACHE_ROOT}/omniverse-logs" \
  "${CACHE_ROOT}/ov-data" \
  "${CACHE_ROOT}/isaaclab-runtime"

docker_gpu_probe() {
  local mode=$1
  local probe_args=(
    --rm
    --entrypoint /bin/true
  )

  case "${mode}" in
    gpus)
      probe_args+=(
        --gpus all
        -e NVIDIA_VISIBLE_DEVICES=all
        -e NVIDIA_DRIVER_CAPABILITIES=all
      )
      ;;
    runtime)
      probe_args+=(
        --runtime=nvidia
        -e NVIDIA_VISIBLE_DEVICES=all
        -e NVIDIA_DRIVER_CAPABILITIES=all
      )
      ;;
    *)
      return 1
      ;;
  esac

  docker run "${probe_args[@]}" "${IMAGE_NAME}:${IMAGE_TAG}" >/dev/null 2>&1
}

DOCKER_ARGS=(
  --rm
  -it
  --name "${CONTAINER_NAME}"
  --network host
  --ipc host
  --ulimit memlock=-1
  --ulimit stack=67108864
  -e ACCEPT_EULA=Y
  -e PRIVACY_CONSENT=Y
  -e OMNI_KIT_ACCEPT_EULA=YES
  -v "${CACHE_ROOT}/ov-cache:/root/.cache/ov:rw"
  -v "${CACHE_ROOT}/pip-cache:/root/.cache/pip:rw"
  -v "${CACHE_ROOT}/uv-cache:/root/.cache/uv:rw"
  -v "${CACHE_ROOT}/hf-cache:/root/.cache/huggingface:rw"
  -v "${CACHE_ROOT}/gl-cache:/root/.cache/nvidia/GLCache:rw"
  -v "${CACHE_ROOT}/compute-cache:/root/.nv/ComputeCache:rw"
  -v "${CACHE_ROOT}/omniverse-logs:/root/.nvidia-omniverse/logs:rw"
  -v "${CACHE_ROOT}/ov-data:/root/.local/share/ov/data:rw"
  -v "${CACHE_ROOT}/isaaclab-runtime:${ISAACLAB_RUNTIME_ROOT}:rw"
  -v "${CACHE_ROOT}/ros-workspaces:/workspace/ros-workspaces:rw"
  -v "${REPO_ROOT}:/workspace/muesli-bt:rw"
)

case "${DOCKER_GPU_MODE}" in
  auto)
    if docker_gpu_probe gpus; then
      DOCKER_ARGS+=(
        --gpus all
        -e NVIDIA_VISIBLE_DEVICES=all
        -e NVIDIA_DRIVER_CAPABILITIES=all
      )
    elif docker_gpu_probe runtime; then
      DOCKER_ARGS+=(
        --runtime=nvidia
        -e NVIDIA_VISIBLE_DEVICES=all
        -e NVIDIA_DRIVER_CAPABILITIES=all
      )
    else
      cat >&2 <<'EOF'
Docker GPU support is not configured correctly on this host.

The wrapper tried both:
  1. docker run --gpus all
  2. docker run --runtime=nvidia

Install and configure NVIDIA Container Toolkit, or set DOCKER_GPU_MODE explicitly:
  DOCKER_GPU_MODE=gpus
  DOCKER_GPU_MODE=runtime
  DOCKER_GPU_MODE=none
EOF
      exit 1
    fi
    ;;
  gpus)
    DOCKER_ARGS+=(
      --gpus all
      -e NVIDIA_VISIBLE_DEVICES=all
      -e NVIDIA_DRIVER_CAPABILITIES=all
    )
    ;;
  runtime)
    DOCKER_ARGS+=(
      --runtime=nvidia
      -e NVIDIA_VISIBLE_DEVICES=all
      -e NVIDIA_DRIVER_CAPABILITIES=all
    )
    ;;
  none)
    ;;
  *)
    echo "Unsupported DOCKER_GPU_MODE: ${DOCKER_GPU_MODE}" >&2
    exit 1
    ;;
esac

if [[ -n "${DISPLAY:-}" ]]; then
  DOCKER_ARGS+=(
    -e "DISPLAY=${DISPLAY}"
    -e QT_X11_NO_MITSHM=1
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw
  )
fi

docker run "${DOCKER_ARGS[@]}" "${IMAGE_NAME}:${IMAGE_TAG}" "$@"
