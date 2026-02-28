# Log Schema (`racecar_demo.v1`)

The demo writes one JSON object per control tick (`JSON Lines`).

## File Locations

- Tick log: `examples/pybullet_racecar/logs/<run_id>.jsonl`
- Metadata: `examples/pybullet_racecar/logs/<run_id>.run_metadata.json`

## Required Tick Fields

- `schema_version` (string, must be `racecar_demo.v1`)
- `run_id` (string)
- `tick_index` (int, monotonic per run)
- `sim_time_s` (float)
- `wall_time_s` (float)
- `mode` (string)
- `state` (object: `x`, `y`, `yaw`, `speed`)
- `goal` (object: `x`, `y`)
- `distance_to_goal` (float)
- `collision_imminent` (bool)
- `action` (object: `steering`, `throttle`)
- `collisions_total` (int)
- `goal_reached` (bool)

## Optional Tick Fields

- `bt` (object)
- `planner` (object)

### `bt` Object

- `status` (`success` | `failure` | `running`)
- `active_path` (list of node names visited in tick order)
- `node_status` (map of node name -> status)

### `planner` Object

- `schema_version` (string, currently `planner.v1`)
- `planner` (`mcts` | `mppi` | `ilqr`)
- `status` (`ok` | `timeout` | `error` | `noaction`)
- `budget_ms` (float)
- `time_used_ms` (float)
- `work_done` (int)
- `confidence` (float)
- `action` (object: `steering`, `throttle`)
- `trace` (optional object, backend-specific)
- `overrun` (optional bool)
- `note` (optional string)

Notes:

- The internal planner metadata uses canonical action format (`action_schema` + `u` vector).
- The racecar demo log projects that action to `{steering, throttle}` for convenience.

#### `planner.trace` (backend-specific)

When planner trace is present, keys follow `planner.v1`:

- `mcts`: `root_visits`, `root_children`, `widen_added`, optional `top_k[{action, visits, q}]`
- `mppi`: `n_samples`, `horizon`, optional `top_k[{action, weight, cost}]`
- `ilqr`: `iters`, `cost_init`, `cost_final`, `reg_final`, optional `top_k`

## Metadata Schema

Required fields:

- `schema_version`
- `run_id`
- `created_utc`
- `git_commit`
- `platform`
- `python_version`
- `pybullet_api_version`
- `seed`
- `config` (full run argument dictionary)

## Versioning Policy

- Additive changes only within `racecar_demo.v1`.
- Any field removal/rename/type change requires a new `schema_version`.
- `run_demo.py` validates top-level fields before writing each record (schema regression guard).
- `planner` sub-object follows `planner.v1`; backend `trace` contents vary by selected planner.
