# Bounded-Time Planning In BTs

`plan-action` lets a behaviour tree run bounded-time MCTS inside a tick and publish best-so-far actions.

## Why Use It

- keep BT control flow explicit (`success`/`failure`/`running`)
- keep planning budget explicit per tick (`budget_ms`, `iters_max`)
- get deterministic replay with seed controls
- keep planner internals observable through structured records

## Tick Flow

Each `plan-action` tick does:

1. read state from blackboard
2. derive or read seed
3. run planner until iteration cap or deadline
4. choose best action (or safe fallback)
5. clamp to model bounds
6. write action to blackboard
7. optionally write compact planning metadata

## Hybrid Patterns

Common combinations with async VLA nodes:

1. VLA intent -> planner refinement: `vla-wait` writes a prior/goal key, then `plan-action` reads it.
2. VLA prior sampler: set planner sampler to `"vla_mixture"` with `action_prior_mean`.
3. Planner-first fallback: run `plan-action` in one branch and keep a VLA request/wait branch for recovery.

`plan-action` accepts `:prior_key`, `:prior_sigma`, and `:prior_mix` for prior-guided sampling.

## Typical Tree Shape

```lisp
(defbt control-loop
  (sel
    (cond goal-reached-1d state 1.0 0.05)
    (seq
      (plan-action
        :name "one-d-planner"
        :model_service "toy-1d"
        :state_key state
        :action_key action
        :meta_key plan-meta
        :budget_ms 8
        :iters_max 800)
      (act apply-planned-1d state action state)
      (running))))
```

## See Also

- [PlanAction Node Reference](plan-action-node.md)
- [Planner Configuration Reference](planner-configuration.md)
- [VLA Integration In BTs](vla-integration.md)
- [Planner Logging Schema](../observability/planner-logging.md)
