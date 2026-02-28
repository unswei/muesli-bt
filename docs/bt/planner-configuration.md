# Planner Configuration Reference

This page describes request/config fields used by `plan-action` and `planner.mcts`.

## Request Fields

- `model_service` (symbol/string): planner model name
- `state` (number or list of numbers): current state
- `seed` (optional int/string/symbol): deterministic seed
- `run_id` (optional symbol/string): record grouping id
- `tick_index` (optional int): record tick index
- `node_name` (optional symbol/string): record node name
- `state_key` (optional symbol/string): state key label for records

## Planner Fields

- `budget_ms` (int): deadline budget for one call
- `iters_max` (int): max simulations
- `gamma` (float): discount
- `max_depth` (int): simulation/rollout depth cap
- `c_ucb` (float): UCB exploration constant
- `pw_k` (float): progressive widening multiplier
- `pw_alpha` (float): progressive widening exponent
- `rollout_policy` (symbol/string): rollout selector hint
- `action_sampler` (symbol/string): sampler selector hint
- `action_prior_mean` (number or numeric list): prior action mean for `\"vla_mixture\"`
- `action_prior_sigma` (float): prior gaussian sigma
- `action_prior_mix` (float): prior mixture ratio in `[0,1]`
- `fallback_action` (number or numeric list): safe fallback action
- `top_k` (int): number of root alternatives to keep in metadata

## Defaults

- `model_service`: `"toy-1d"`
- `budget_ms`: `20`
- `iters_max`: `2000`
- `gamma`: `0.95`
- `max_depth`: `25`
- `c_ucb`: `1.2`
- `pw_k`: `2.0`
- `pw_alpha`: `0.5`
- `top_k`: `3`
- `action_prior_sigma`: `0.2`
- `action_prior_mix`: `0.5`

## Model Services Included

- `"toy-1d"`: scalar state/action toy control model
- `"ptz-track"`: 2D pan/tilt tracking model

## Lisp Service Example

```lisp
(begin
  (define req (map.make))
  (map.set! req 'model_service "toy-1d")
  (map.set! req 'state 0.0)
  (map.set! req 'budget_ms 8)
  (map.set! req 'iters_max 600)
  (map.set! req 'seed 42)
  (map.set! req 'action_sampler "vla_mixture")
  (map.set! req 'action_prior_mean (list 0.2))
  (map.set! req 'action_prior_sigma 0.15)
  (map.set! req 'action_prior_mix 0.7)
  (planner.mcts req))
```

## See Also

- [PlanAction Node Reference](plan-action-node.md)
- [Planner Logging Schema](../observability/planner-logging.md)
- [Built-ins Overview](../language/builtins.md)
