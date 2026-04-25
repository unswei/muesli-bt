# Planner Configuration Reference

This page documents the unified planner request schema used by `planner.plan` and `plan-action`.

## Request Schema

- `schema_version` must be `"planner.request.v1"`
- `planner` one of `mcts | mppi | ilqr`
- `model_service` registered model name
- `state` numeric list (or scalar)
- `budget_ms` hard budget in milliseconds

## Common Fields

- `seed`: deterministic seed
- `safe_action`: canonical action map `{action_schema, u}`
- `action_schema`: default schema for result action
- `work_max`: secondary cap (iterations/samples/optimiser iterations)
- `horizon`: rollout horizon for horizon-based backends
- `dt_ms`: optional timestep hint
- `bounds`: list of `[lo hi]` rows (one per action dim)
- `constraints`: optional map (`max_du`, `smoothness_weight`, `collision_weight`, `goal_tolerance`)
- `top_k`: number of candidates in trace output

## Backend Maps

### `mcts`

- `c_ucb`, `pw_k`, `pw_alpha`
- `max_depth`, `gamma`
- `rollout_policy`, `action_sampler`

### `mppi`

- `lambda`
- `sigma` (per-dim or scalar list)
- `n_samples`, `n_elite`
- `u_init`, `u_nominal`

### `ilqr`

- `max_iters`
- `reg_init`, `reg_factor`
- `tol_cost`, `tol_grad`
- `u_init`
- `derivatives` = `analytic | finite_diff`
- `fd_eps` for finite differences

## `plan-action` Mapping

`plan-action` builds `planner.request.v1` internally. Common BT keys:

- `:planner`, `:budget_ms`, `:work_max`, `:horizon`, `:dt_ms`
- `:model_service`, `:state_key`, `:action_key`, `:meta_key`
- `:seed_key`, `:safe_action`, `:safe_action_key`, `:action_schema`
- backend keys (examples): `:max_depth`, `:n_samples`, `:max_iters`, `:derivatives`

Node fallback rule: if planner status is not `:ok`, the node returns `failure`.
The planner result still carries an action map.
On timeout, no-action, or error paths, that action is the resolved safe action unless the planner produced a valid bounded candidate before timing out.

## Result And Logging

`planner.plan` returns `planner.result.v1` with:

- `status`: `:ok`, `:timeout`, `:noaction`, or `:error`
- `action`: canonical `{action_schema, u}` map
- `confidence`
- `stats`: `budget_ms`, `time_used_ms`, `work_done`, `seed`, and optional `overrun` / `note`
- optional backend `trace`
- optional `error`

Each call also records a `planner.v1` event payload through the canonical `planner_v1` event type when canonical events are enabled.

## Example (`planner.plan`)

```lisp
(begin
  (define req (map.make))
  (map.set! req 'schema_version "planner.request.v1")
  (map.set! req 'planner "mcts")
  (map.set! req 'model_service "toy-1d")
  (map.set! req 'state (list 0.0))
  (map.set! req 'budget_ms 10)
  (map.set! req 'work_max 300)
  (define mcts (map.make))
  (map.set! mcts 'max_depth 18)
  (map.set! req 'mcts mcts)
  (planner.plan req))
```

## See Also

- [Planning Overview](../planning/overview.md)
- [planner.plan Request/Result](../planning/planner-plan.md)
- [PlanAction Node Reference](plan-action-node.md)
- [Bounded-Time Planning](bounded-time-planning.md)
- [Planner Logging Schema](../observability/planner-logging.md)
