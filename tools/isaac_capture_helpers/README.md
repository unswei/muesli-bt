# Isaac capture helpers

This directory contains auxiliary scripts for staging and recording the Isaac Sim TurtleBot3 wheeled demo on a remote Linux host.

These helpers are useful when:

- Isaac Sim is installed on a remote NVIDIA machine
- the TurtleBot3 asset has already been imported into USD
- you want a quick way to stage a polished capture scene
- you want a reproducible way to export a clip and still image from the viewport
- you want to capture the live ROS 2-driven demo even when Isaac Sim and ROS 2 use different Python versions

The recommended path is direct viewport export. X11 display recording remains available as a fallback when you need to inspect the live window.

## files

- `show_tb3_scene.py`
  Opens Isaac Sim, loads a simple TurtleBot3 capture scene, positions a third-person camera, and animates the robot along a short showcase route.

- `export_tb3_showcase.py`
  Renders a polished TurtleBot3 showcase sequence to PNG frames, copies a representative still to `scene.png`, and encodes the final clip to `showcase.mp4`.

- `ros2_udp_bridge.py`
  Bridges localhost UDP state and command packets onto `/robot/odom`, `/robot/cmd_vel`, and `/tf` so the live Isaac scene can work with a standard ROS 2 Humble install.

- `live_ros2_tb3_showcase.py`
  Runs the live TurtleBot3 Isaac scene, captures viewport frames from the BT-driven run, copies a still image, and encodes the short showcase clip.

- `run_live_ros2_tb3_capture.sh`
  Starts `Xvfb` when needed, launches the UDP bridge, runs the live Isaac capture scene, and then runs `examples/repl_scripts/ros2-flagship-goal.lisp`.

- `run_tb3_record_manual.sh`
  Starts a manual `Xvfb` display, runs `show_tb3_scene.py`, and attempts to record the virtual display to MP4 with `ffmpeg`.

## expected host setup

These scripts assume:

- Ubuntu Linux
- NVIDIA GPU drivers working
- an Isaac Sim Python environment already activated or available
- a TurtleBot3 USD at:
  `/home/deploy/turtlebot3_imported/turtlebot3_burger/turtlebot3_burger.usda`
- `ffmpeg` and `Xvfb` installed

## typical usage

On the remote host:

```bash
source /home/deploy/env_isaaclab/bin/activate
python tools/isaac_capture_helpers/show_tb3_scene.py
```

For the scripted preview export:

```bash
export OMNI_KIT_ACCEPT_EULA=YES
export DISPLAY=:88
source /home/deploy/env_isaaclab/bin/activate
python tools/isaac_capture_helpers/export_tb3_showcase.py \
  --usd-path /home/deploy/turtlebot3_imported/turtlebot3_burger/turtlebot3_burger.usda \
  --output-dir /home/deploy/isaac_tb3_showcase
```

For the live ROS 2-driven capture:

```bash
export OMNI_KIT_ACCEPT_EULA=YES
bash tools/isaac_capture_helpers/run_live_ros2_tb3_capture.sh
```

The live helper writes:

```text
/home/deploy/isaac_tb3_live_ros2/showcase.mp4
/home/deploy/isaac_tb3_live_ros2/scene.png
/home/deploy/isaac_tb3_live_ros2/state_trace.jsonl
```

It also drives the usual ROS 2 run logs under `build/linux-ros2/`.

For the scripted preview route:

```bash
export OMNI_KIT_ACCEPT_EULA=YES
export DISPLAY=:88
source /home/deploy/env_isaaclab/bin/activate
python tools/isaac_capture_helpers/export_tb3_showcase.py \
  --usd-path /home/deploy/turtlebot3_imported/turtlebot3_burger/turtlebot3_burger.usda \
  --output-dir /home/deploy/isaac_tb3_showcase
```

Use the live helper for the bundled website media. Use the scripted preview helper when you only want to check the scene, camera, and asset path without starting the ROS 2 loop.

For a manual recording attempt:

```bash
source /home/deploy/env_isaaclab/bin/activate
bash tools/isaac_capture_helpers/run_tb3_record_manual.sh
```

## outputs

The recording helper writes:

- `/home/deploy/show_tb3_scene.log`
- `/home/deploy/ffmpeg_tb3.log`
- `/home/deploy/xvfb_tb3.log`
- `/home/deploy/tb3_display.mp4`

The scripted export writes:

- `<output-dir>/showcase.mp4`
- `<output-dir>/scene.png`
- `<output-dir>/frames/frame_*.png`

The live capture helper writes:

- `/home/deploy/isaac_tb3_live_ros2/showcase.mp4`
- `/home/deploy/isaac_tb3_live_ros2/scene.png`
- `/home/deploy/isaac_tb3_live_ros2/frames/frame_*.png`
- `/home/deploy/isaac_tb3_live_ros2/state_trace.jsonl`

The manual recording helper writes:

- `/home/deploy/show_tb3_scene.log`
- `/home/deploy/ffmpeg_tb3.log`
- `/home/deploy/xvfb_tb3.log`
- `/home/deploy/tb3_display.mp4`
