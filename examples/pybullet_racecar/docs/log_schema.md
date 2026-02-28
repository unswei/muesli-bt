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
- `budget_ms` (float)
- `time_used_ms` (float)
- `iters` (int)
- `root_visits` (int)
- `root_children` (int)
- `widen_added` (int)
- `depth_max` (int)
- `depth_mean` (float)
- `status` (`ok` | `timeout` | `noaction`)
- `confidence` (float)
- `value_est` (float)
- `action` (object: `steering`, `throttle`)
- `top_k` (list of objects with `action`, `visits`, `q`)

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
