# Demo Walkthrough

## Task Definition

The agent drives a PyBullet racecar from start pose to a goal marker while avoiding static box obstacles.

In BT modes, the control loop is executed by `muesli_bt_bridge` and the C++ builtin `env.pybullet.run-loop`
(observe -> BT tick -> action extract -> actuation -> sim step -> log callback).

State used by control:

- pose: `x`, `y`, `yaw`
- speed: signed forward speed
- goal vector
- short-range raycast distances

Action:

- `steering` in `[-1, 1]`
- `throttle` in `[-1, 1]` (positive drive, negative reverse/brake)

## Controller Modes

- `manual`: keyboard control only
- `bt_basic`: constant-action BT leaf (sanity path)
- `bt_obstacles`: selector with avoid branch vs drive-to-goal branch
- `bt_planner`: selector with safety branch plus bounded-time MCTS planner branch

## BT Structure (Planner Mode)

```text
Selector
  Sequence
    GoalReachedRacecar?
    Succeed
  Sequence
    CollisionImminent?
    AvoidObstacle
    Running
  Sequence
    PlanActionNode
    ApplyAction
    Running
```

The goal branch is terminal. Otherwise the tree keeps returning `running` while driving.

## Planner Defaults

- budget: `20 ms`
- iterations cap: `1200`
- depth: `18`
- discount: `0.96`
- progressive widening: `k=2.0`, `alpha=0.5`
- action space: continuous `(steering, throttle)`

Transition model is a lightweight kinematic approximation. Real execution still runs in PyBullet.

## Logging and Introspection

Each tick emits a JSONL record with schema `racecar_demo.v1`:

- base run metadata (`run_id`, `mode`, `tick_index`, `sim_time_s`)
- observed state + chosen action
- BT status (BT modes)
- planner diagnostics (planner mode): budget, used time, visits, widening, confidence, top-k actions

Every run also writes `run_metadata.json` containing:

- git commit hash
- platform and Python info
- pybullet API version
- seed and full config

## Plot Interpretation

- **budget_adherence**: budget line vs `time_used_ms`
- **planner_growth**: root visits/children and widening additions over ticks
- **confidence**: share of root visits on selected action
- **action_traces**: steering/throttle changes through time
- **topk_visit_distribution**: aggregate concentration across candidate actions
