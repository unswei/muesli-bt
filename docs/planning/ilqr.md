# iLQR Backend

iLQR (iterative Linear Quadratic Regulator) is a deterministic trajectory optimiser.

It alternates forward rollout, backward pass, and line search under regularization.

## Best Fit

- smooth dynamics/cost models
- low-noise deterministic optimisation settings
- cases where derivative quality is available

## Required Model Support

Always required:

- action dimension/bounds
- rollout/state propagation support

For derivatives:

- `derivatives = analytic` (preferred): model provides linearized dynamics and quadraticized costs
- `derivatives = finite_diff` (fallback): backend computes finite differences around `(x,u)`

If required derivative support is unavailable, planner returns `status=:error`.

## Request Config Block (`request.ilqr`)

- `max_iters`
- `reg_init`
- `reg_factor`
- `tol_cost`
- `tol_grad`
- `u_init` (optional)
- `derivatives`: `analytic | finite_diff`
- `fd_eps` (for finite differences)

## Trace Fields

`trace.ilqr` may include:

- `iters`
- `cost_init`
- `cost_final`
- `reg_final`
- optional `top_k`

## Notes

- confidence is based on convergence/cost reduction quality
- finite differences are slower and less stable for large dimensions

## See Also

- [Planner Request/Result](planner-plan.md)
- [Planner Logging](../observability/planner-logging.md)
