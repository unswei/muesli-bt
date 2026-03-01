# MCTS Backend

MCTS (Monte Carlo Tree Search) is useful when you want explicit lookahead and branch-wise diagnostics.

In muesli-bt, the MCTS backend uses progressive widening for continuous/large action spaces.

## Best Fit

- sparse, discontinuous, or non-smooth action effects
- situations where top-k candidate actions are useful for debugging
- when tree diagnostics (`visits`, `q`) matter

## Required Model Support

- `step(state, action, rng) -> (next_state, reward, done)`
- `action_dim()`
- action bounds (model-provided or request `bounds`)

## Request Config Block (`request.mcts`)

- `c_ucb`
- `pw_k`
- `pw_alpha`
- `max_depth`
- `gamma`
- `rollout_policy`
- `action_sampler`

## Trace Fields

`trace.mcts` may include:

- `root_visits`
- `root_children`
- `widen_added`
- `top_k`: `{action, visits, q}`

## Notes

- confidence is derived from root visit concentration
- `budget_ms` + `work_max` jointly cap expansion/rollouts

## See Also

- [Planner Request/Result](planner-plan.md)
- [Planner Logging](../observability/planner-logging.md)
