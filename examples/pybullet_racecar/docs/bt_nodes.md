# BT Node Overview

## `bt_basic`

- `constant-drive` (action callback)
  - Writes constant action `[0.0, 0.45]` to blackboard key `action`.
- `apply-action` (action callback)
  - Validates action shape for host extraction.

## `bt_obstacles`

- `goal-reached-racecar` (condition callback)
- `collision-imminent` (condition callback)
- `avoid-obstacle` (action callback)
- `drive-to-goal` (action callback)
- `apply-action` (action callback)

Behavior:

- Goal condition returns success.
- Otherwise when forward rays are short, avoidance steers to the clearer side.
- If no hazard is active, proportional goal tracking drives toward the marker.

## `bt_planner`

- `goal-reached-racecar` (condition callback)
- `collision-imminent` (condition callback)
- `avoid-obstacle` (action callback)
- `plan-action` (planner node, model `racecar-kinematic-v1`)
- `apply-action` (action callback)

Behavior:

- Safety branch preempts planning.
- Planner branch runs planner-agnostic `plan-action` (internally `planner.plan`); this demo config selects `:planner "mcts"`.
- `plan-action` returns `success` only when planner status is `:ok`; on `:timeout`/`:error`/`:noaction`, the branch fails.
- Host loop applies fallback `[0.0, 0.0]` if no valid planner action is available after the BT tick.

## Introspection

Per tick logs keep `racecar_demo.v1` unchanged, with optional:

- `bt.status`
- `planner` block derived from unified planner meta (`planner.v1`) when planning executes
