# cross-transport comparison protocol

## what this is

This page defines the first concrete comparison protocol for the `v0.5.0` flagship.

The goal is to compare the shared wheeled flagship across:

- PyBullet
- Webots
- ROS2

using one shared normalisation path, while the fixed collection window remains the PyBullet and Webots portability pair.

This protocol is intentionally narrow.
It is the first evidence protocol for the flagship, not the final evaluation design for `1.0`.
It is a portability protocol, not the stricter same-robot comparison track.
ROS2 support in the normalisation tooling does not, by itself, mean this fixed paper-facing collection protocol has expanded to a three-way parity claim.

## when to use it

Use this page when you:

- collect a matched PyBullet and Webots flagship pair
- want the shared tooling to stay compatible with the ROS2 flagship wrapper
- want one repeatable command set for the current comparison tooling
- need to know which metrics count as primary evidence and which are only secondary context

Do not use this page when you want to justify near-identical decisions for the same physical robot.

## how it works

### current protocol choice

Use a shared prefix window, not full-run terminal equality, as the primary comparison unit.

This is acceptable only because the current pair is:

- Webots e-puck
- PyBullet racecar

That pair can support a portability claim:

- same BT semantics
- same task class
- same shared contract

It cannot support a strong same-robot claim.

Reason:

- Webots currently runs the flagship to goal in the checked-in cluttered world
- PyBullet now runs the flagship correctly, but the practical Mac headless collection path is a shorter sampled run
- forcing both backends into one identical full-run termination rule right now would slow iteration without improving the protocol logic

So the protocol is:

1. keep the shared controller settings fixed
2. collect backend logs under those settings
3. compare the first `320` ticks at `20 Hz`
4. record full-run terminal outcome only as secondary context

That gives a clear shared evidence window:

- `320` ticks
- `16.0` nominal seconds
- same planner budget and shared command contract

### fixed controller settings

Use these settings for the current protocol:

- `goal_tolerance_m = 0.25`
- `collision_threshold = 0.75`
- planner: `mcts`
- `budget_ms = 10`
- `work_max = 280`
- `max_depth = 18`
- `gamma = 0.96`
- `pw_k = 2.0`
- `pw_alpha = 0.5`
- `tick_hz = 20`
- shared action schema: `flagship.cmd.v1`
- shared planner model: `flagship-goal-shared-v1`

These values are recorded in:

- `examples/flagship_wheeled/configs/shared_thresholds.json`
- `examples/flagship_wheeled/configs/comparison_protocol.json`

### primary metrics

Treat these as the primary evidence metrics for the current protocol:

- branch trace agreement over the aligned `320`-tick prefix
- goal-distance mean absolute difference over the aligned prefix
- shared action mean absolute difference for `linear_x`
- shared action mean absolute difference for `angular_z`

### secondary metrics

Record these as secondary context only:

- full-run final status
- full-run final goal distance
- planner-used tick counts outside the shared prefix

## api / syntax

### protocol config

The checked-in protocol config is:

- `examples/flagship_wheeled/configs/comparison_protocol.json`

### collection commands

PyBullet:

```bash
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_racecar/run_demo.py \
  --mode bt_flagship \
  --headless \
  --tick-hz 20 \
  --duration-sec 16 \
  --budget-ms 10 \
  --work-max 280 \
  --max-depth 18 \
  --gamma 0.96 \
  --pw-k 2.0 \
  --pw-alpha 0.5 \
  --seed 7
```

Webots:

```bash
MUESLI_BT_WEBOTS_LISP_ENTRY=lisp/flagship_entry.lisp \
  "$WEBOTS_HOME/webots" --batch --mode=fast --stdout --stderr \
  examples/webots_epuck_goal/worlds/epuck_goal_cluttered.wbt
```

### normalise and compare

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

python3 examples/flagship_wheeled/tools/compare_runs.py \
  --max-ticks 320 \
  examples/flagship_wheeled/out/pybullet_flagship.json \
  examples/flagship_wheeled/out/webots_flagship.json
```

## example

Interpretation of the current protocol output:

- the aligned prefix answers whether the same BT stays logically similar across the first `16` seconds
- the full-run final status answers whether one backend reaches the goal under the same controller shape
- if the prefix metrics are poor, fix the wrapper mapping or scenario before expanding to ROS2

## gotchas

- Do not compare a short PyBullet sanity run against the full Webots run without `--max-ticks 320`.
- Do not treat ROS2 normalisation support as proof that the fixed PyBullet/Webots portability protocol has already become a three-way equivalence protocol.
- Do not change planner budget or thresholds between backends inside the same comparison pair.
- Do not treat full-run terminal mismatch as the primary failure signal yet. The current protocol is prefix-first by design.
- Do not change the shared BT branch order just to improve one backend’s numbers.
- Do not use this portability protocol to argue for same-robot decision equivalence.

## see also

- [cross-transport flagship for v0.5](cross-transport-flagship.md)
- [same-robot strict comparison track](same-robot-strict-comparison.md)
- [cross-transport shared contract](cross-transport-shared-contract.md)
- [cross-transport flagship comparison](../examples/cross-transport-flagship-comparison.md)
