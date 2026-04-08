# PyBullet e-puck-style Goal Seeking

This example runs the shared wheeled flagship BT against a compact differential-drive surrogate in PyBullet.

It keeps the flagship decision contract visible:

- `goal_reached`
- `collision_imminent`
- `planner_state = [goal_dist, goal_bearing, obstacle_front, speed]`
- `act_avoid`
- `act_goal_direct`
- `action_cmd = [linear_x, angular_z]`

The Python runner owns the PyBullet scene, observation shaping, wheel-speed mapping, and JSONL logging.

## Run

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

## Outputs

- tick log: `examples/pybullet_epuck_goal/logs/<run_id>.jsonl`
- metadata: `examples/pybullet_epuck_goal/logs/<run_id>.run_metadata.json`

## Key Files

- `bt/flagship_entry.lisp`
- `run_demo.py`
