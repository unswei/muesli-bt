# Planner Logging Schema

Planner calls emit `planner.v1` records to:

- in-memory ring buffer (`planner.logs.dump`)
- JSONL file sink (`logs/planner-records.jsonl` by default)

Use:

- `(planner.set-log-path "path/to/planner.jsonl")`
- `(planner.set-log-enabled #t)` / `(planner.set-log-enabled #f)`
- `(planner.logs.dump [n])`

## `planner.v1` Core Fields

- `schema_version` (`"planner.v1"`)
- `planner` (`"mcts" | "mppi" | "ilqr"`)
- `status` (`ok | timeout | error | noaction`)
- `budget_ms`
- `time_used_ms`
- `work_done`
- `action` (canonical action map: `{action_schema, u}`)
- `confidence`
- optional `trace`

Runtime metadata such as `run_id`, `tick_index`, `node_name`, and `seed` is also included.

## Planner-Specific `trace`

### MCTS

- `root_visits`
- `root_children`
- `widen_added`
- optional `top_k`: `{action, visits, q}`

### MPPI

- `n_samples`
- `horizon`
- optional `top_k`: `{action, weight, cost}`

### iLQR

- `iters`
- `cost_init`
- `cost_final`
- `reg_final`

## Reusable Visualisations

The `planner.v1` schema is designed so the same plots work across backends:

- budget adherence: `time_used_ms` vs `budget_ms`
- confidence trend: `confidence` over ticks
- root action distribution: `trace.*.top_k` when present

For backends without `top_k` (often iLQR), budget/confidence plots still apply.

## Example JSON Line

```json
{"schema_version":"planner.v1","planner":"mppi","status":"ok","budget_ms":12,"time_used_ms":9,"work_done":96,"action":{"action_schema":"action.u.v1","u":[0.41]},"confidence":0.63,"trace":{"n_samples":96,"horizon":10}}
```

## See Also

- [PlanAction Node Reference](../bt/plan-action-node.md)
- [Planner Configuration Reference](../bt/planner-configuration.md)
