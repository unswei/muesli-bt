# `plan-action` Node

`plan-action` is a planner-agnostic BT leaf that calls `planner.plan` internally.

## What It Does Per Tick

1. builds a `planner.request.v1` from node options and blackboard state
2. calls planner backend selected by `:planner`
3. writes `result.action` to `:action_key`
4. optionally writes planner metadata to `:meta_key`
5. returns node status based on planner status

## Core Options

- `:planner` (`:mcts | :mppi | :ilqr`)
- `:budget_ms`
- `:work_max`
- `:model_service`
- `:state_key`
- `:action_key`
- `:meta_key` (optional)
- `:safe_action` / `:safe_action_key` (optional)
- `:action_schema`, `:top_k` (optional)

Backend-specific options map into request blocks (`mcts`, `mppi`, `ilqr`).

## Fallback Policy

Recommended policy:

- use BT fallback branches when `plan-action` fails
- keep backend-side safe action as final guard

Current runtime behaviour is strict:

- node returns `success` only when planner status is `:ok`
- non-`ok` planner statuses return `failure`

## Example

```lisp
(plan-action
  :name "racecar-plan"
  :planner :mcts
  :budget_ms 20
  :work_max 1200
  :model_service "racecar-kinematic-v1"
  :state_key state
  :action_key action
  :meta_key plan-meta
  :top_k 5)
```

## See Also

- [Planning Overview](overview.md)
- [planner.plan Request/Result](planner-plan.md)
- [BT Semantics](../bt/semantics.md)
