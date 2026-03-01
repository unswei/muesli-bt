# `planner.plan`

`planner.plan` is the unified planner entrypoint.

Signature:

```lisp
(planner.plan request-map) -> result-map
```

## Request Schema (`planner.request.v1`)

Required:

- `schema_version`: `"planner.request.v1"`
- `planner`: `"mcts" | "mppi" | "ilqr"`
- `model_service`: registered model name
- `state`: numeric state vector (or model-supported handle)
- `budget_ms`: hard budget target

Common optional:

- `seed`
- `work_max`
- `horizon`, `dt_ms`
- `bounds`
- `constraints`
- `safe_action` and `action_schema`
- backend block: `mcts`, `mppi`, or `ilqr`

Canonical action map:

```lisp
(map.make 'action_schema "action.u.v1" 'u (list 0.0 0.0))
```

## Result Schema (`planner.result.v1`)

Required:

- `schema_version`: `"planner.result.v1"`
- `planner`: backend name
- `status`: `:ok | :timeout | :error | :noaction`
- `action`: canonical action map
- `confidence`
- `stats` (`budget_ms`, `time_used_ms`, `work_done`, `seed`)

Optional:

- `trace` backend diagnostics
- `stats.overrun`, `stats.note`
- `error` message

## Minimal Example

```lisp
(let ((req (map.make)))
  (map.set! req 'schema_version "planner.request.v1")
  (map.set! req 'planner "mppi")
  (map.set! req 'model_service "toy-1d")
  (map.set! req 'state (list -1.0))
  (map.set! req 'budget_ms 10)
  (map.set! req 'work_max 128)
  (map.set! req 'mppi (map.make 'lambda 0.8 'sigma (list 0.25) 'n_samples 128))
  (planner.plan req))
```

## Model Interface Summary

- all planners require `step(state, action, rng)` and action dimension/bounds support
- MPPI/iLQR are horizon-oriented and require efficient rollout support
- iLQR requires derivative support (`analytic` or configured finite differences)

## See Also

- [Planner Overview](overview.md)
- [MCTS Backend](mcts.md)
- [MPPI Backend](mppi.md)
- [iLQR Backend](ilqr.md)
- [Built-in Reference: `planner.plan`](../language/reference/builtins/planner/planner-plan.md)
