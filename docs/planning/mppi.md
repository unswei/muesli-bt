# MPPI Backend

MPPI (Model Predictive Path Integral control) is a sampling-based MPC method.

It optimises a horizon action sequence by sampling noise, evaluating rollout costs, and updating the nominal sequence with weighted samples.

## Best Fit

- continuous control with reasonably smooth costs
- short-horizon receding control
- systems where many rollouts per tick are affordable

## Required Model Support

- action dimension/bounds
- rollout support via either:
  - direct `rollout_cost(state, action_sequence)` (recommended), or
  - repeated `step` with cheap state cloning

## Request Config Block (`request.mppi`)

- `lambda` (temperature)
- `sigma` (per-dimension noise std)
- `n_samples`
- `n_elite` (optional)
- `u_init` (optional warm start)
- `u_nominal` (optional warm start)

Also uses common fields: `horizon`, `work_max`, `constraints`, `bounds`.

## Trace Fields

`trace.mppi` may include:

- `n_samples`
- `horizon`
- optional `top_k`: `{action, weight, cost}`

## Notes

- confidence can be derived from sample-weight concentration
- `work_max` typically caps samples per tick

## See Also

- [Planner Request/Result](planner-plan.md)
- [Planner Logging](../observability/planner-logging.md)
