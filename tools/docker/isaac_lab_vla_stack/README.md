# muesli-bt Isaac Lab + VLA image

## what this is

This image packages a robotics research stack for `linux/amd64`:

- Ubuntu 22.04 userland on NVIDIA's CUDA runtime base
- Isaac Lab `v2.3.1` source checkout
- ROS 2 Humble (`ros-base` by default)
- SmolVLA through LeRobot `v0.4.1`
- OpenPI / `pi0.5` pinned to commit `e6b044115c075dc64a2e4e23d33ea3a3c293f123`

`muesli-bt` is a compact Lisp runtime with an integrated behaviour tree engine, bounded-time planning, and asynchronous vision-language-action orchestration for robotics and control.

Project repository:

- [github.com/unswei/muesli-bt](https://github.com/unswei/muesli-bt)
- Docker Hub repository: [evilrobots/muesli-bt-isaac-lab-vla](https://hub.docker.com/r/evilrobots/muesli-bt-isaac-lab-vla)

## published image

The public image is published on Docker Hub as:

- [`evilrobots/muesli-bt-isaac-lab-vla:latest`](https://hub.docker.com/r/evilrobots/muesli-bt-isaac-lab-vla)

For reproducible pulls, `latest` currently resolves to:

- `sha256:fcf1fa8f72baf3d203ef4d1d5f15d267633a3203b4aeca6b97461dd4f4d41f8c`

## what is included

The image is designed as a practical development and serving base for:

- Isaac Lab simulation workflows
- ROS 2 Humble integration work
- SmolVLA and OpenPI policy serving
- `muesli-bt` VLA backend experiments

The image keeps Isaac Lab runtime pieces, SmolVLA, and OpenPI in separate Python environments so Isaac Lab, Torch, JAX, and Transformers do not fight each other.

## what is not included

Isaac Sim itself is not baked into the image.

Instead, the image ships an `install-isaacsim` helper that installs Isaac Sim `5.1.0` from NVIDIA's official pip packages into a persistent mounted runtime directory the first time you need it. This keeps the published image leaner and avoids redistributing Isaac Sim inside the Docker image itself.

## container layout

Installed layout inside the container:

- Isaac Lab source: `/opt/robotics/IsaacLab`
- Isaac Lab runtime mount: `/opt/robotics/runtime/isaaclab`
- Isaac Lab venv: `/opt/robotics/runtime/isaaclab/venv`
- LeRobot source: `/opt/robotics/lerobot`
- OpenPI source: `/opt/robotics/openpi`
- SmolVLA venv: `/opt/venvs/smolvla`
- OpenPI venv: `/opt/venvs/openpi`

Useful wrapper commands:

- `install-isaacsim`: install Isaac Sim and finish the Isaac Lab runtime setup
- `isaaclab-python`: run Python inside the Isaac Lab / Isaac Sim environment
- `enter-isaaclab`: open an interactive shell with the Isaac Lab runtime environment activated
- `install-isaac-h1-policy`: clone/build NVIDIA's official H1 ROS2 policy workspace
- `isaac-h1-policy`: launch `h1_fullbody_controller` from the official ROS2 workspace
- `verify-isaac-h1-topics`: wait for the checked-in H1 hero-demo ROS topics
- `isaac-h1-hero`: build `muslisp_ros2` and run the checked-in H1 hero demo
- `isaac-sim-state`: query or change Isaac Sim state through ROS 2 simulation-control services
- `smolvla-python`: run Python inside the SmolVLA environment
- `smolvla-policy-server`: start LeRobot's async policy server
- `openpi-python`: run Python inside the OpenPI environment
- `openpi-serve-policy`: start `scripts/serve_policy.py` from OpenPI
- `enter-smolvla`: open an interactive shell with the SmolVLA environment activated
- `enter-openpi`: open an interactive shell with the OpenPI environment activated

## build from source

From the repository root:

```bash
./tools/docker/isaac_lab_vla_stack/build.sh
```

Version pins and image defaults live in `tools/docker/isaac_lab_vla_stack/versions.env`.

## pull published image

Pull the public image with:

```bash
docker pull evilrobots/muesli-bt-isaac-lab-vla:latest
```

Or pin the exact published digest:

```bash
docker pull evilrobots/muesli-bt-isaac-lab-vla@sha256:fcf1fa8f72baf3d203ef4d1d5f15d267633a3203b4aeca6b97461dd4f4d41f8c
```

Run it directly:

```bash
docker run --rm -it --gpus all evilrobots/muesli-bt-isaac-lab-vla:latest bash
```

Some older Docker + NVIDIA setups still require the legacy runtime flag:

```bash
docker run --rm -it \
  --runtime=nvidia \
  -e NVIDIA_VISIBLE_DEVICES=all \
  -e NVIDIA_DRIVER_CAPABILITIES=all \
  evilrobots/muesli-bt-isaac-lab-vla:latest bash
```

## publish manually

Log in to Docker Hub first:

```bash
docker login
```

Then publish a refreshed image from the repository root:

```bash
./tools/docker/isaac_lab_vla_stack/publish.sh
```

Publish a specific tag instead:

```bash
./tools/docker/isaac_lab_vla_stack/publish.sh v0.1.0
```

This pushes `linux/amd64` only, which is the intended public target for this image.

## run from source

Start an interactive shell:

```bash
./tools/docker/isaac_lab_vla_stack/run.sh
```

`run.sh` auto-detects whether the host supports Docker GPU access via `--gpus all` or the older `--runtime=nvidia` path. You can override that detection if needed:

```bash
DOCKER_GPU_MODE=gpus ./tools/docker/isaac_lab_vla_stack/run.sh
DOCKER_GPU_MODE=runtime ./tools/docker/isaac_lab_vla_stack/run.sh
DOCKER_GPU_MODE=none ./tools/docker/isaac_lab_vla_stack/run.sh bash
```

Install Isaac Sim and Isaac Lab into the persistent runtime mount:

```bash
./tools/docker/isaac_lab_vla_stack/run.sh install-isaacsim
```

This follows NVIDIA's supported pip-based Isaac Sim install flow:

- `pip install "isaacsim[all,extscache]==5.1.0" --extra-index-url https://pypi.nvidia.com`
- `./isaaclab.sh --install`

Run some quick checks:

```bash
./tools/docker/isaac_lab_vla_stack/run.sh isaaclab-python -c "import isaacsim, isaaclab; print('ok')"
./tools/docker/isaac_lab_vla_stack/run.sh smolvla-python -c "import lerobot; print(lerobot.__version__)"
./tools/docker/isaac_lab_vla_stack/run.sh openpi-python -c "import openpi; print(openpi.__file__)"
```

## H1 hero demo flow

The repository now includes a ROS-backed H1 locomotion hero demo that keeps `muesli-bt` on the current `Odometry` -> `Twist` contract surface.

Bootstrap the official H1 policy workspace inside the container:

```bash
./tools/docker/isaac_lab_vla_stack/run.sh install-isaac-h1-policy
```

Inside the container, launch the H1 policy node:

```bash
isaac-h1-policy
```

Once Isaac Sim is publishing the required `/h1_01/*` topics, verify them and run the hero demo:

```bash
verify-isaac-h1-topics
isaac-sim-state play
isaac-h1-hero
```

The hero demo writes:

- `logs/isaac_h1_hero.run-loop.jsonl`
- `logs/isaac_h1_hero.mbt.evt.v1.jsonl`

The topic contract checked into the repository is:

- `examples/isaac_h1_ros2_hero/isaac/topic_contract.yaml`

The run script mounts this repository at `/workspace/muesli-bt` and persists caches under `~/.cache/muesli-bt/isaac-lab-vla/`. The Isaac Lab runtime install is persisted under `~/.cache/muesli-bt/isaac-lab-vla/isaaclab-runtime/`, so `install-isaacsim` is normally a one-time setup per host cache.

## prerequisites

- Linux host on `amd64` / `x86_64`
- NVIDIA GPU
- Docker Engine with NVIDIA Container Toolkit configured so GPU containers can start
- network access from inside the container for the first Isaac install

## gotchas

- This is a Linux NVIDIA workflow. It is not a practical runtime target for macOS.
- The first `install-isaacsim` run downloads a very large Isaac Sim payload and can take a while.
- `run.sh` will try both Docker GPU integration styles: `--gpus all` first, then `--runtime=nvidia`. If both fail, fix the host's NVIDIA Container Toolkit installation.
- `smolvla-policy-server` starts LeRobot's async gRPC server. You still need a client or adapter layer to send policy instructions and observations.
- `openpi-serve-policy` does not download checkpoints for you. Point it at your own checkpoint or a supported remote path.
- If you need full ROS desktop tools instead of `ros-base`, set `ROS2_APT_PACKAGE=desktop` in `tools/docker/isaac_lab_vla_stack/versions.env` before building.
- If you want Isaac Lab's extra learning frameworks, pass one when installing, for example `install-isaacsim rl_games`, or set `ISAACLAB_FRAMEWORKS=all` in `tools/docker/isaac_lab_vla_stack/versions.env` before building.

## more information

- [muesli-bt repository](https://github.com/unswei/muesli-bt)
- [muesli-bt VLA integration notes](https://github.com/unswei/muesli-bt/blob/main/docs/bt/vla-integration.md)
- [muesli-bt VLA request/response contract](https://github.com/unswei/muesli-bt/blob/main/docs/bt/vla-request-response.md)
