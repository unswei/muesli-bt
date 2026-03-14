#!/usr/bin/env bash
set -euo pipefail

: "${OPENPI_REF:?OPENPI_REF must be set}"
: "${OPENPI_ROOT:?OPENPI_ROOT must be set}"
: "${OPENPI_VENV:?OPENPI_VENV must be set}"
: "${OPENPI_PYTHON_VERSION:?OPENPI_PYTHON_VERSION must be set}"

mkdir -p "$(dirname -- "${OPENPI_ROOT}")" "$(dirname -- "${OPENPI_VENV}")"

git clone https://github.com/Physical-Intelligence/openpi.git "${OPENPI_ROOT}"
git -C "${OPENPI_ROOT}" checkout "${OPENPI_REF}"
GIT_LFS_SKIP_SMUDGE=1 git -C "${OPENPI_ROOT}" submodule update --init --recursive

export UV_PROJECT_ENVIRONMENT="${OPENPI_VENV}"
export UV_LINK_MODE=copy
export GIT_LFS_SKIP_SMUDGE=1

uv venv --python "${OPENPI_PYTHON_VERSION}" "${OPENPI_VENV}"

pushd "${OPENPI_ROOT}" >/dev/null
uv sync --frozen --no-dev
uv pip install --python "${OPENPI_VENV}/bin/python" -e .

transformers_dir=$("${OPENPI_VENV}/bin/python" -c 'import pathlib, transformers; print(pathlib.Path(transformers.__file__).resolve().parent)')
cp -a "${OPENPI_ROOT}/src/openpi/models_pytorch/transformers_replace/." "${transformers_dir}/"
popd >/dev/null
