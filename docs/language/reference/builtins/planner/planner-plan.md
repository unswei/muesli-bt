# `planner.plan`

**Signature:** `(planner.plan request-map) -> result-map`

## What It Does

Runs a planner backend selected in `request-map` and returns a unified `planner.result.v1` map.

## Required Request Fields

- `schema_version` must be `"planner.request.v1"`
- `planner` one of `"mcts" | "mppi" | "ilqr"` (symbol/string also accepted)
- `model_service` planner model name
- `state` numeric state (number or numeric list)
- `budget_ms` integer time budget

## Optional Common Fields

- `seed`, `safe_action`, `action_schema`
- `work_max`, `horizon`, `dt_ms`, `bounds`, `constraints`, `top_k`
- backend config maps: `mcts`, `mppi`, `ilqr`

## Result Shape

- `schema_version` = `"planner.result.v1"`
- `planner` = `"mcts" | "mppi" | "ilqr"`
- `status` = `:ok | :timeout | :error | :noaction`
- `action` canonical action map: `{action_schema, u}`
- `confidence` in `[0,1]`
- `stats` with at least `budget_ms`, `time_used_ms`, `work_done`, `seed`
- optional `trace` and `error`

## Example

```lisp
(begin
  (define req (map.make))
  (map.set! req 'schema_version "planner.request.v1")
  (map.set! req 'planner "mppi")
  (map.set! req 'model_service "toy-1d")
  (map.set! req 'state 0.0)
  (map.set! req 'budget_ms 12)
  (map.set! req 'work_max 128)
  (map.set! req 'horizon 10)
  (define mppi (map.make))
  (map.set! mppi 'lambda 1.0)
  (map.set! mppi 'sigma (list 0.3))
  (map.set! mppi 'n_samples 128)
  (map.set! req 'mppi mppi)
  (planner.plan req))
```

## See Also

- [Planner Configuration](../../../../bt/planner-configuration.md)
- [PlanAction Node](../../../../bt/plan-action-node.md)
