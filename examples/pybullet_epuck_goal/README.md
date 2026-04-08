# pybullet e-puck-style goal seeking

This example runs the shared wheeled flagship BT against a compact differential-drive surrogate in PyBullet.

The default scene is a simple success path. It is meant to answer one question quickly: does the shared wheeled flagship BT run cleanly on a small differential-drive robot in PyBullet and reach the goal? The checked-in clutter layout also reaches the goal, so it is useful both for inspection and for stricter same-robot comparison work.

It keeps the flagship decision contract visible:

- `goal_reached`
- `collision_imminent`
- `planner_state = [goal_dist, goal_bearing, obstacle_front, speed]`
- `act_avoid`
- `act_goal_direct`
- `action_cmd = [linear_x, angular_z]`

The Python runner owns the PyBullet scene, observation shaping, wheel-speed mapping, JSONL logging, and optional screenshot export.

## run

```bash
make demo-setup
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --headless
```

For a GUI run:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py
```

Enable the checked-in clutter layout:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --with-default-obstacles
```

Capture a scene preview:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py \
  --headless \
  --with-default-obstacles \
  --camera-distance 1.35 \
  --camera-yaw 38 \
  --camera-pitch -63 \
  --screenshot-target-x 0.08 \
  --screenshot-target-y 0.05 \
  --screenshot-path docs/assets/demos/pybullet-epuck-goal/scene.png
```

Capture a short MP4 clip:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py \
  --headless \
  --with-default-obstacles \
  --camera-distance 1.35 \
  --camera-yaw 38 \
  --camera-pitch -63 \
  --screenshot-target-x 0.08 \
  --screenshot-target-y 0.05 \
  --video-path docs/assets/demos/pybullet-epuck-goal/showcase.mp4 \
  --video-fps 12 \
  --video-every-nth-tick 4
```

`ffmpeg` is required for `--video-path`.

## outputs

- tick log: `examples/pybullet_epuck_goal/logs/<run_id>.jsonl`
- metadata: `examples/pybullet_epuck_goal/logs/<run_id>.run_metadata.json`
- optional preview image: any path passed via `--screenshot-path`
- optional MP4 clip: any path passed via `--video-path`

## key files

- `bt/flagship_entry.lisp`
- `run_demo.py`
