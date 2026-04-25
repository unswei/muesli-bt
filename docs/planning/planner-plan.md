# `planner.plan`

`planner.plan` is the unified planner entrypoint.

It is the bounded in-runtime decision planner surface.
It is not a generic host service call and should not absorb external services such as MoveIt, Nav2, detector pipelines, or simulator-specific task APIs.

Use a [host capability bundle](../integration/host-capability-bundles.md) when a higher-level host service needs its own contract.

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

- `seed`: deterministic seed; strings and symbols are hashed
- `work_max`: secondary work cap for iterations, samples, or optimiser steps
- `horizon`, `dt_ms`: rollout horizon and timestep hints
- `bounds`: action bounds as `(list (list lo hi) ...)`
- `constraints`: optional map (`max_du`, `smoothness_weight`, `collision_weight`, `goal_tolerance`)
- `safe_action` and `action_schema`: fallback action shape and default action schema
- `run_id`, `tick_index`, `node_name`, `state_key`: optional observability metadata
- `top_k`: number of planner trace candidates to expose where supported
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

## statuses and fallback

`status` is one of:

- `:ok`: planner selected an action within its normal success path
- `:timeout`: planner exceeded the requested budget target but still returns the best available action
- `:noaction`: planner did work but did not produce a usable candidate
- `:error`: request, model, derivative, or backend validation failed

`planner.plan` always returns an action map.
When the planner cannot produce a usable action, the returned action is the resolved safe action.
The safe action comes from `safe_action` when provided; otherwise the planner service derives a bounded default from the selected model and action schema.

`plan-action` treats non-`:ok` statuses as node failure.
Use BT fallback branches or backend-side safe action handling for non-`:ok` results.

## budgets and work caps

`budget_ms` is the primary bounded-time target.
`work_max` is a secondary cap whose meaning depends on the backend:

- MCTS: tree iterations
- MPPI: sample/evaluation work
- iLQR: optimiser iterations

The planner records `stats.time_used_ms`, `stats.work_done`, and `stats.overrun`.
`stats.overrun` means elapsed planner time was greater than `budget_ms`.
Small overruns can still happen because the runtime checks time at planner decision points rather than pre-empting arbitrary C++ code.

## boundary

Keep `planner.plan` focused on bounded action selection.

Belongs in `planner.plan`:

- request/result maps using `planner.request.v1` and `planner.result.v1`
- planner choice such as `mcts`, `mppi`, or `ilqr`
- model-service selection
- state, horizon, budget, work cap, bounds, and safe action fields
- planner stats and diagnostics
- `planner.v1` records emitted as canonical `planner_v1` events

Does not belong in `planner.plan`:

- direct middleware messages
- robot-driver handles
- MoveIt, Nav2, detector, or simulator-specific RPCs
- long-running execution services that need progress, cancellation, or host lifecycle ownership

Use host capability bundles for those external services.

## logging

Every `planner.plan` call appends a `planner.v1` record.
When canonical events are enabled, the runtime emits that record through event type `planner_v1`.

The record includes:

- `run_id`, `tick_index`, `node_name`, and optional `state_key`
- `planner`, `status`, `budget_ms`, `time_used_ms`, `work_done`, `seed`
- selected or safe action
- `confidence`, `overrun`, optional `note`
- backend trace fields where available

Use [Planner Logging](../observability/planner-logging.md) for the event wrapper details.

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
- [Host Capability Bundles](../integration/host-capability-bundles.md)
- [MCTS Backend](mcts.md)
- [MPPI Backend](mppi.md)
- [iLQR Backend](ilqr.md)
- [Built-in Reference: `planner.plan`](../language/reference/builtins/planner/planner-plan.md)
