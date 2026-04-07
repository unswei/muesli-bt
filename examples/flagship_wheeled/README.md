# flagship wheeled example

This directory is the backend-neutral home for the `v0.5.0` cross-transport flagship.

It is intended to own:

- the canonical shared BT
- shared helper formulas and blackboard shaping helpers
- comparison and normalisation tooling
- shared thresholds used by cross-transport runs

Current status:

- shared BT and helper files are checked in
- Webots and PyBullet flagship wrappers now run
- normalisation and comparison tooling is available under `tools/`
- ROS2 wrapper is still a scaffold

Planned backend wrappers:

- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`
- `examples/pybullet_racecar/bt/flagship_entry.lisp`
- `examples/repl_scripts/ros2-flagship-goal.lisp`

See also:

- `docs/integration/cross-transport-flagship.md`
- `docs/integration/cross-transport-shared-contract.md`
- `docs/integration/cross-transport-comparison-protocol.md`

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
