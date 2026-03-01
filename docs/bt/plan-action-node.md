# PlanAction Node Reference

## Signature

```lisp
(plan-action key value key value ...)
```

`plan-action` is planner-agnostic and always routes through `planner.plan`.

## Core Keys

- `:name` node/record name
- `:planner` backend (`:mcts`, `:mppi`, `:ilqr`)
- `:budget_ms` budget per tick
- `:work_max` secondary work cap
- `:horizon`, `:dt_ms`
- `:model_service`
- `:state_key`, `:action_key`
- `:meta_key` optional JSON metadata key
- `:seed_key` optional deterministic seed key
- `:safe_action` / `:safe_action_key`
- `:action_schema`

## Backend Keys

### MCTS

- `:c_ucb`, `:pw_k`, `:pw_alpha`, `:max_depth`, `:gamma`
- `:rollout_policy`, `:action_sampler`

### MPPI

- `:lambda`, `:sigma`, `:sigma_key`
- `:n_samples`, `:n_elite`

### iLQR

- `:max_iters`, `:reg_init`, `:reg_factor`
- `:tol_cost`, `:tol_grad`
- `:derivatives` (`:analytic` or `:finite_diff`)
- `:fd_eps`

## Constraint Keys

- `:max_du` / `:max_du_key`
- `:smoothness_weight`
- `:collision_weight`
- `:goal_tolerance`
- `:top_k`

Unknown keys are configuration errors.

## Return Semantics

- writes `result.action` to `:action_key`
- writes compact planner JSON to `:meta_key` when provided
- returns `success` only when planner status is `:ok`
- returns `failure` for missing state/service/config errors, planner errors, `:timeout`, or `:noaction`

## Example

```lisp
(plan-action
  :name "drive-plan"
  :planner :mppi
  :model_service "toy-1d"
  :state_key state
  :action_key action
  :meta_key plan-meta
  :budget_ms 12
  :work_max 128
  :horizon 10
  :lambda 1.0
  :sigma 0.3
  :n_samples 128)
```

## See Also

- [Planning: `plan-action` Node](../planning/plan-action-node.md)
- [Planner Configuration Reference](planner-configuration.md)
- [Bounded-Time Planning](bounded-time-planning.md)
- [Planner Logging Schema](../observability/planner-logging.md)
