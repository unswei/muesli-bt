#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

apt_retry() {
  local -a cmd=("$@")
  local attempt

  for attempt in 1 2 3 4 5; do
    if "${cmd[@]}"; then
      return 0
    fi

    rm -rf /var/lib/apt/lists/*
    if [[ "${attempt}" -lt 5 ]]; then
      echo "apt command failed on attempt ${attempt}; retrying..." >&2
      sleep $((attempt * 5))
    fi
  done

  echo "apt command failed after 5 attempts: ${cmd[*]}" >&2
  return 1
}

apt_retry apt-get -o Acquire::Retries=5 update
apt_retry apt-get install -y --no-install-recommends \
  bash-completion \
  build-essential \
  ca-certificates \
  clang \
  cmake \
  curl \
  ffmpeg \
  git \
  git-lfs \
  gnupg \
  libavcodec-dev \
  libavdevice-dev \
  libavfilter-dev \
  libavformat-dev \
  libavutil-dev \
  libegl1 \
  libegl1-mesa \
  libgl1 \
  libglib2.0-0 \
  libglu1-mesa \
  libsm6 \
  libswresample-dev \
  libswscale-dev \
  libx11-6 \
  libxcursor1 \
  libxinerama1 \
  libxi6 \
  libxext6 \
  libxkbcommon0 \
  libxrandr2 \
  libxrender1 \
  libxshmfence1 \
  lsb-release \
  ncurses-term \
  pkg-config \
  python3 \
  python3-pip \
  python3-venv \
  software-properties-common \
  wget \
  xz-utils

git lfs install --system

apt-get clean
rm -rf /var/lib/apt/lists/*
