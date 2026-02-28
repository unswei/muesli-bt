# Bounded-Time Planning In BTs

`plan-action` runs `planner.plan` under an explicit per-tick budget.

## Why Use It

- planner backend can be switched per node (`mcts`, `mppi`, `ilqr`)
- bounded execution via `budget_ms` and `work_max`
- deterministic replay via `seed` / `seed_key`
- unified diagnostics (`planner.v1` + planner-specific `trace`)

## Tick Flow

1. read state from blackboard
2. build `planner.request.v1`
3. call planner backend until budget/work cap
4. write action to blackboard
5. optionally write planner metadata JSON
6. return `success` only when status is `:ok`

## Typical Tree Shape

```lisp
(defbt control-loop
  (sel
    (cond goal-reached-1d state 1.0 0.05)
    (seq
      (plan-action
        :name "one-d-planner"
        :planner :mcts
        :model_service "toy-1d"
        :state_key state
        :action_key action
        :meta_key plan-meta
        :budget_ms 10
        :work_max 600)
      (act apply-planned-1d state action state)
      (running))))
```

## Backend Selection

- `:planner :mcts` for tree-search style lookahead
- `:planner :mppi` for sampling MPC
- `:planner :ilqr` for deterministic optimization (requires derivatives policy support)

Use BT control flow to decide when to switch backend or fallback branch.

## See Also

- [PlanAction Node Reference](plan-action-node.md)
- [Planner Configuration Reference](planner-configuration.md)
- [Planner Logging Schema](../observability/planner-logging.md)
