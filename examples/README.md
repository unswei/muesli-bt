# Examples

This directory contains runnable muesli-bt examples.

## New Webots e-puck demos (`env.*`)

- `webots_epuck_obstacle/`: obstacle avoidance + wall following with BT branch switching.
- `webots_epuck_line/`: line following with BT + bounded planner confidence logging.
- `webots_epuck_common/`: shared Webots e-puck controller implementation used by both demos.
- `_tools/`: reusable JSONL plotting scripts (`plot_bt_timeline.py`, `plot_planner_root.py`).

## Existing examples

- `bt/`: small BT language/runtime examples.
- `repl_scripts/`: standalone Lisp algorithm and planner scripts.
- `pybullet_racecar/`: PyBullet racecar showcase.
- `pybullet_racecar_common/`: shared C++ extension plumbing for the PyBullet racecar demo.
