# BT Node Overview

## `bt_basic`

- `ApplyConstantDrive` (action)
  - Writes constant action `{steering=0.0, throttle=0.45}`.

## `bt_obstacles`

- `RootSelector` (selector)
  - `AvoidBranch` (sequence)
    - `CollisionImminent?` (condition)
    - `AvoidObstacle` (action)
  - `GoalBranch` (sequence)
    - `DriveToGoal` (action)

Behavior:

- When any forward ray falls below threshold, the avoid branch takes control.
- Otherwise a simple proportional goal tracker drives toward the marker.

## `bt_planner`

- `RootSelector` (selector)
  - `AvoidBranch` (sequence)
    - `CollisionImminent?`
    - `AvoidObstacle`
  - `PlannerBranch` (sequence)
    - `PlanActionNode`
    - `ApplyAction`

Behavior:

- Safety branch preempts planning.
- Planner branch runs bounded-time continuous MCTS and writes best-so-far action.
- If planner yields no action, branch fails and safe fallback action is applied.

## Introspection

Per tick, logs include:

- active path (`bt.active_path`)
- per-node statuses (`bt.node_status`)

These are stable fields in `racecar_demo.v1`.
