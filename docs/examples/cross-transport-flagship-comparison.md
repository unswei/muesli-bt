# Cross-transport Flagship Comparison

## What This Is

This page shows how to compare the shared `v0.5` flagship behaviour across backends.

The comparison flow has two steps:

1. normalise each backend log into one shared JSON schema
2. compare those normalised runs on shared behaviour traces

The current tooling supports:

- PyBullet flagship logs
- Webots flagship logs

ROS2 is still pending.

The concrete collection protocol now lives in [cross-transport comparison protocol](../integration/cross-transport-comparison-protocol.md).

## When To Use It

Use this page when you want to:

- check whether the Webots and PyBullet flagship runs follow the same branch pattern
- compare goal distance and shared action traces
- produce one backend-neutral summary before adding ROS2 to the same comparison set

## How It Works

`normalise_run.py` converts backend-specific logs into one shared schema:

- `schema_version`
- `backend`
- `run_id`
- `summary`
- `final`
- `ticks[]`

Each normalised tick keeps only the shared comparison surface:

- `tick_index`
- `time_s`
- `goal_dist`
- `goal_reached`
- `branch`
- `shared_action`
- `planner_used`

`compare_runs.py` then compares two or more normalised runs against the first input as the baseline.

The current comparison summary reports:

- branch trace agreement
- goal-distance mean absolute difference
- shared action mean absolute difference for `linear_x` and `angular_z`
- final outcome for each backend

For the current `v0.5` protocol, use a fixed aligned prefix window:

- `20 Hz`
- `320` ticks
- `16.0` nominal seconds

Run the comparator with `--max-ticks 320` so both backends are judged on the same prefix window.

## API / Syntax

Normalise a PyBullet flagship run:

```bash
python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend pybullet \
  --output examples/flagship_wheeled/out/pybullet_flagship.json \
  examples/pybullet_racecar/logs/<run_id>.jsonl
```

Normalise a Webots flagship run:

```bash
python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend webots \
  --output examples/flagship_wheeled/out/webots_flagship.json \
  examples/webots_epuck_goal/logs/flagship_goal.jsonl
```

Compare the two normalised runs:

```bash
python3 examples/flagship_wheeled/tools/compare_runs.py \
  --max-ticks 320 \
  examples/flagship_wheeled/out/pybullet_flagship.json \
  examples/flagship_wheeled/out/webots_flagship.json
```

Optional machine-readable output:

```bash
python3 examples/flagship_wheeled/tools/compare_runs.py \
  --json-out examples/flagship_wheeled/out/pybullet_vs_webots.json \
  examples/flagship_wheeled/out/pybullet_flagship.json \
  examples/flagship_wheeled/out/webots_flagship.json
```

## Example

Example normalised comparison summary:

```text
baseline: pybullet (...) ticks=30 final_goal_dist=7.093 goal_reached=False
compare: webots (...) aligned_ticks=30 same_goal_reached=False
  branch_agreement=0.800 (24/30)
  goal_dist_mae=6.3732
  shared_action_mae.linear_x=0.2871
  shared_action_mae.angular_z=0.1964
```

The exact values will vary with scenario duration and controller pacing.

## Gotchas

- Compare flagship logs only. Older `bt_planner` or non-flagship Webots logs do not expose the same shared command trace.
- Use runs with similar scenario setup and duration. The scripts do not infer scenario equivalence for you.
- For the current protocol, use `--max-ticks 320` on the comparison step.
- The first input to `compare_runs.py` is the baseline.
- The current comparator aligns by tick index, not by resampled wall-clock time.

## See Also

- [cross-transport flagship for v0.5](../integration/cross-transport-flagship.md)
- [cross-transport comparison protocol](../integration/cross-transport-comparison-protocol.md)
- [cross-transport shared contract](../integration/cross-transport-shared-contract.md)
- [Webots: e-puck goal seeking](webots-epuck-goal-seeking.md)
- [PyBullet: racecar](pybullet-racecar.md)
