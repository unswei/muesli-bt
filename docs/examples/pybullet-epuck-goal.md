# PyBullet: e-puck-style Goal Seeking

## What It Demonstrates

- the shared wheeled flagship BT on a compact differential-drive surrogate
- a PyBullet path that keeps `[linear_x, angular_z]` visible all the way to the robot command surface
- a stricter embodiment match to the Webots e-puck family than the racecar demo provides
- JSONL logs that can be inspected alongside the existing cross-transport tooling
- a clean default scene for successful first-run verification, with an optional clutter layout for manual inspection

## Run It

```bash
make demo-setup
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --headless
```

GUI run:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py
```

Optional clutter layout:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --with-default-obstacles
```

## What To Look For

- the flagship BT still switches between `goal_reached`, `avoid`, `planner`, and `direct_goal`
- the simulator maps shared flagship commands into differential left/right wheel motion
- the robot reaches the goal without introducing a simulator-specific BT fork
- the log keeps the branch, planner, and shared-action surfaces visible

## Logs

- tick log: `examples/pybullet_epuck_goal/logs/<run_id>.jsonl`
- metadata: `examples/pybullet_epuck_goal/logs/<run_id>.run_metadata.json`

## Key Files

- `examples/pybullet_epuck_goal/bt/flagship_entry.lisp`
- `examples/pybullet_epuck_goal/run_demo.py`

## BT Source (Inline)

```lisp
--8<-- "examples/pybullet_epuck_goal/bt/flagship_entry.lisp"
```

Full walkthrough:

- [PyBullet e-puck-style goal full source page](pybullet-epuck-goal-source.md)

## See Also

- [Webots: e-puck Goal Seeking](webots-epuck-goal-seeking.md)
- [same-robot strict comparison track](../integration/same-robot-strict-comparison.md)
