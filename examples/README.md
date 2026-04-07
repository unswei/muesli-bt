# Examples

This directory contains runnable muesli-bt examples.

## New Webots e-puck demos (`env.*`)

- `flagship_wheeled/`: shared `v0.5` cross-transport BT, shared contract helpers, thresholds, and comparison-tool scaffolding.
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
- `repl_scripts/ros2-live-odom-twist.lisp`: minimal live ROS2 control-loop script for the Ubuntu 22.04 + Humble `Odometry` -> `Twist` baseline.
- `repl_scripts/ros2-flagship-goal.lisp`: shared-flagship ROS2 wrapper that derives the cross-transport contract from odometry plus fixed scenario geometry.
- `repl_scripts/ros2_flagship_test_publisher.py`: simple ROS2 odometry publisher for exercising the flagship wrapper against a deterministic straight-line goal trajectory.
- `isaac_h1_ros2_demo/`: ROS2 H1 locomotion demo assets, including the checked-in Lisp BT, runtime wrapper, and Isaac topic contract.
- `isaac_wheeled_ros2_demo/`: topic contract and runbook assets for the planned Isaac-backed wheeled flagship showcase.
- `pybullet_racecar/`: PyBullet racecar showcase, including the shared `bt_flagship` cross-transport mode.
- `../integrations/pybullet/`: shared C++ integration plumbing used by the PyBullet racecar demo.

## Script Extension Convention

Example Lisp entrypoints use `.lisp`.
The older `.muslisp`/`.mueslisp` naming is equivalent semantically, but `.lisp` is the project convention.
