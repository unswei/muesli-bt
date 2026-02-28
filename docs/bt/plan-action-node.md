# PlanAction Node Reference

## Signature

```lisp
(plan-action key value key value ...)
```

`plan-action` is a BT DSL node (not a general Lisp special form). It runs a bounded-time planner call and writes action output to blackboard.

## Core Keys

- `:name` planner node name used in records
- `:budget_ms` hard time budget per tick
- `:iters_max` max simulations per tick
- `:model_service` host planner model name (for example `"toy-1d"` or `"ptz-track"`)
- `:state_key` blackboard key to read state
- `:action_key` blackboard key to write action
- `:meta_key` optional key to write compact JSON metadata
- `:seed_key` optional key to read deterministic seed
- `:fallback_action` optional scalar fallback action

## Planner Parameters

- `:gamma`
- `:max_depth`
- `:c_ucb`
- `:pw_k`
- `:pw_alpha`
- `:rollout_policy`
- `:action_sampler`
- `:top_k`

Unknown keys are treated as configuration errors.

## Return Semantics

- `success` when an action is produced and written
- `failure` when state is missing/invalid, planner service is unavailable, or planner errors occur

The node is intentionally single-tick (`running` is not used by this node itself).

## Deterministic Seeding

- if `:seed_key` exists and is readable, that value is used
- otherwise seed is derived from planner base seed + node name + tick index

## Example

```lisp
(plan-action
  :name "ptz-planner"
  :model_service "ptz-track"
  :state_key ptz-state
  :action_key ptz-action
  :meta_key ptz-meta
  :budget_ms 12
  :iters_max 1200
  :gamma 0.96
  :max_depth 26
  :c_ucb 1.25
  :pw_k 2.2
  :pw_alpha 0.55)
```

## See Also

- [Bounded-Time Planning In BTs](bounded-time-planning.md)
- [Planner Configuration Reference](planner-configuration.md)
- [Planner Logging Schema](../observability/planner-logging.md)
