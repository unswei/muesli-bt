# flagship wheeled example

This directory contains the reusable wheeled behaviour, shared helper code, and log-normalisation tools used by the main wheeled examples.

It includes:

- the canonical shared BT
- an experimental generated-recovery BT variant
- an experimental navigation-capability BT variant
- shared helper formulas and blackboard shaping helpers
- comparison and normalisation tooling
- shared thresholds used by the wheeled runs

Current contents:

- shared BT and helper files are checked in
- the generated-recovery variant is checked in for fixture/runtime evidence only
- the navigation-capability variant is checked in for `cap.navigation.v1` evidence only
- Webots and PyBullet wrappers run against the shared behaviour
- ROS2 flagship normalisation is supported by the shared tooling
- normalisation and comparison tooling is available under `tools/`
- ROS2 wrapper remains a thin host-side derivation over the released `Odometry` -> `Twist` surface

Current backend wrappers:

- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`
- `examples/pybullet_racecar/bt/flagship_entry.lisp`
- `examples/repl_scripts/ros2-flagship-goal.lisp`

The backend wrappers intentionally keep loading `lisp/bt_goal_flagship.lisp`.
`lisp/bt_goal_flagship_generated_recovery.lisp` is an experimental pre-`v1.0`
variant that represents the collision recovery branch as a patchable
`recovery-policy` slot. Use it for generated-subtree validation, install, and
rollback evidence, not for cross-transport comparison baselines yet.

`lisp/bt_goal_flagship_nav_capability.lisp` is an experimental pre-`v1.0`
variant that delegates the goal-seeking lane to `cap.navigation.v1`. Use it for
core mock capability evidence and ROS2 fake-action-server unit coverage, not for
cross-transport comparison baselines yet.

See also:

- `docs/integration/cross-transport-flagship.md`
- `docs/integration/cross-transport-shared-contract.md`
- `docs/integration/cross-transport-comparison-protocol.md`
- `docs/evidence/wheeled-flagship-nav-capability.md`

## comparison workflow

Normalise backend logs into the shared comparison schema:

```bash
python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend pybullet \
  --output examples/flagship_wheeled/out/pybullet_flagship.json \
  examples/pybullet_racecar/logs/<run_id>.jsonl

python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend webots \
  --output examples/flagship_wheeled/out/webots_flagship.json \
  examples/webots_epuck_goal/logs/flagship_goal.jsonl

python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend ros2 \
  --output examples/flagship_wheeled/out/ros2_flagship.json \
  build/linux-ros2/ros2-flagship-goal.jsonl
```

Then compare the normalised runs:

```bash
python3 examples/flagship_wheeled/tools/compare_runs.py \
  examples/flagship_wheeled/out/pybullet_flagship.json \
  examples/flagship_wheeled/out/webots_flagship.json
```

The current comparison summary reports:

- branch trace agreement
- goal-distance mean absolute difference
- shared action mean absolute difference for `linear_x` and `angular_z`
- final outcome for each backend
