# Examples

This directory contains runnable muesli-bt examples.

## New Webots e-puck demos (`env.*`)

- `webots_epuck_obstacle/`: obstacle avoidance + wall following with BT branch switching.
- `webots_epuck_line/`: line following with BT + bounded planner confidence logging.
- `webots_epuck_goal/`: goal seeking in a cluttered arena with lidar-lite observations.
- `webots_epuck_foraging/`: coloured puck foraging (search/approach/return) with planner-gated motion.
- `webots_epuck_tag/`: pursuit-evasion (tag) with strict-budget interception behaviour.
- `webots_epuck_common/`: shared Webots e-puck controller implementation used by all e-puck Webots demos.
- `_tools/`: reusable JSONL plotting scripts (`plot_bt_timeline.py`, `plot_planner_root.py`, `plot_success_rate.py`).

## Existing examples

- `bt/`: small BT language/runtime examples.
- `repl_scripts/`: standalone Lisp algorithm and planner scripts.
- `pybullet_racecar/`: PyBullet racecar showcase.
- `pybullet_racecar_common/`: shared C++ extension plumbing for the PyBullet racecar demo.

## Script Extension Convention

Example Lisp entrypoints use `.lisp`.
The older `.muslisp`/`.mueslisp` naming is equivalent semantically, but `.lisp` is the project convention.
