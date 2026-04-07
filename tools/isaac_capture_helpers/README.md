# Isaac capture helpers

This directory contains auxiliary scripts for staging and recording the Isaac Sim TurtleBot3 wheeled demo on a remote Linux host.

These helpers are useful when:

- Isaac Sim is installed on a remote NVIDIA machine
- the TurtleBot3 asset has already been imported into USD
- you want a quick way to stage a polished capture scene
- you want to experiment with viewport recording through `Xvfb` and `ffmpeg`

These helpers are not part of the main demo path, are not exercised in CI, and should be treated as operator tools rather than supported product interfaces.

## files

- `show_tb3_scene.py`
  Opens Isaac Sim, loads a simple TurtleBot3 capture scene, positions a third-person camera, and animates the robot along a short showcase route.

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

These paths are intentionally explicit so they are easy to inspect after a remote run.
