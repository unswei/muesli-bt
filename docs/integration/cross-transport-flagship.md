# cross-transport flagship for v0.5

## what this is

This page records the next design decision for `v0.5.0`.

The goal is to make one wheeled behaviour the cross-transport flagship for:

- PyBullet
- Webots
- ROS2

The flagship should prove one claim clearly:

The same high-level BT can run across those transports while backend-specific sensing and actuation stay at the host boundary.

For a stricter “almost identical decisions under the same circumstances” claim, the project should use a same-robot comparison track rather than the current e-puck versus racecar pairing.

This page is a design note for the `v0.5.0` milestone.
It is not a new runtime contract.

The concrete shared blackboard and command specification lives in [cross-transport shared contract](cross-transport-shared-contract.md).

## when to use it

Use this page when you:

- choose the first shared wheeled behaviour for `v0.5.0`
- decide which inputs belong in the shared BT contract
- decide which differences must stay backend-specific
- review whether a proposed demo still fits the released ROS2 `Odometry` -> `Twist` scope

## how it works

### chosen starting point

Use the existing Webots cluttered-goal demo as the starting point, not the line-following or wall-following demos.

Reason:

- the behaviour is easier to express from pose, heading, velocity, goal, and bounded obstacle context
- that shape fits the released ROS2 `Odometry` -> `Twist` surface much more naturally than simulator-specific line or wall sensors do
- PyBullet already has a close goal-seeking racecar path with collision handling and planner support

### shared behaviour shape

The shared BT should stay small and deterministic:

1. if the goal is reached, succeed
2. if collision risk is high, choose a safe avoid action
3. otherwise try bounded planning toward the goal
4. if planning does not yield an action, fall back to a deterministic direct-to-goal action

The shared tree should not use random roam as the canonical fallback.
Random roam is acceptable in a backend-specific demo, but it is weak evidence for cross-transport comparison.

### shared blackboard contract

Required shared inputs:

- `goal_reached`: boolean
- `collision_imminent`: boolean
- `goal_dist`: distance to the configured goal
- `goal_bearing`: signed heading error to the goal
- `pose_x`: current x position
- `pose_y`: current y position
- `yaw`: current heading
- `speed`: current forward speed
- `obstacle_front`: bounded front obstacle score or clearance-derived risk
- `planner_state`: normalised planner input vector
- `act_avoid`: deterministic avoid command in the shared command shape
- `act_goal_direct`: deterministic direct-to-goal command in the shared command shape

Recommended shared outputs:

- `planner_action`: planner-selected command in the shared command shape
- `planner_meta`: planner diagnostics
- `action_cmd`: final chosen command in the shared command shape
- `active_branch`: stable branch id for logs and comparison

### shared command intent

For the flagship, the shared command intent should be:

- `linear_x`
- `angular_z`

Represent that intent inside the shared BT and blackboard as a normalised two-value command vector:

```text
[linear_x, angular_z]
```

This keeps the shared BT close to the released ROS2 surface while still allowing backend-specific action mapping:

- ROS2 maps directly to `geometry_msgs/msg/Twist`
- Webots maps to left/right wheel commands
- PyBullet maps to steering/throttle

The normalised command intent is the important shared artefact.
The raw actuator command should remain backend-specific.

### backend-specific responsibilities

These responsibilities should stay outside the shared BT:

- deriving `goal_dist`, `goal_bearing`, and `obstacle_front` from backend observations
- converting the shared `[linear_x, angular_z]` intent into backend actuator commands
- configuring topic names, simulator worlds, reset policy, and transport wiring
- exposing backend-native telemetry beyond the shared comparison set

### proposed repository layout

The cleanest implementation shape is:

1. one shared flagship example directory for backend-neutral BT logic and comparison tooling
2. one thin wrapper in each backend-specific example that maps native observations and actions to the shared contract

Recommended layout:

```text
examples/flagship_wheeled/
  README.md
  lisp/
    bt_goal_flagship.lisp
    contract_helpers.lisp
  tools/
    compare_runs.py
    normalise_run.py
  configs/
    shared_thresholds.json

examples/webots_epuck_goal/
  lisp/
    flagship_entry.lisp

examples/pybullet_racecar/
  bt/
    flagship_entry.lisp

examples/repl_scripts/
  ros2-flagship-goal.lisp
```

Responsibility split:

- `examples/flagship_wheeled/`
  - owns the canonical BT
  - owns shared helper formulas and comparison tooling
  - owns the documented scenario thresholds used for cross-transport runs

- `examples/webots_epuck_goal/lisp/flagship_entry.lisp`
  - loads the shared BT
  - derives shared keys from Webots observations
  - converts shared command intent into wheel commands

- `examples/pybullet_racecar/bt/flagship_entry.lisp`
  - loads the shared BT
  - derives shared keys from racecar state and rays
  - converts shared command intent into steering and throttle

- `examples/repl_scripts/ros2-flagship-goal.lisp`
  - loads the shared BT
  - derives shared keys from odometry plus fixed goal and obstacle geometry
  - publishes the shared command intent through `ros2.action.v1`

This keeps existing demo directories intact while giving the flagship its own backend-neutral home.

### implementation checklist

Build this in the following order:

1. freeze the shared keys, units, thresholds, and command intent from [cross-transport shared contract](cross-transport-shared-contract.md)
2. add `examples/flagship_wheeled/lisp/bt_goal_flagship.lisp` with no backend-specific sensor or actuator assumptions
3. add `contract_helpers.lisp` for shared formulas such as `goal_dist`, `goal_bearing`, and deterministic direct-goal fallback
4. wire Webots first by adding a thin `flagship_entry.lisp` on top of the existing cluttered-goal demo
5. wire PyBullet second by mapping racecar state and rays into the same shared keys
6. wire ROS2 third by keeping the current `Odometry` -> `Twist` surface and deriving obstacle and goal context in the host wrapper
7. add comparison scripts that check branch trace, goal-distance trace, final outcome, and action trace in the shared command intent
8. add one end-to-end docs page that shows the shared BT once and then explains only the backend-specific wrapper differences
9. freeze one concrete PyBullet/Webots comparison protocol before expanding the same flow to ROS2

Definition of done for the implementation phase:

- one shared BT file is loaded by all three backend paths
- backend wrappers are each short and mostly limited to normalisation plus command conversion
- no backend wrapper changes the BT branch order or branch meaning
- cross-transport comparisons use the shared command intent, not backend-native actuator logs

## api / syntax

Proposed canonical BT skeleton:

```lisp
(defbt wheeled_goal_flagship
  (sel
    (seq
      (cond bb-truthy goal_reached)
      (succeed))
    (seq
      (cond bb-truthy collision_imminent)
      (act select-action act_avoid 1 action_cmd)
      (running))
    (seq
      (plan-action :name "goal-plan"
                   :planner :mcts
                   :model_service "flagship-goal-shared-v1"
                   :state_key planner_state
                   :action_key planner_action
                   :meta_key planner_meta
                   :action_schema "flagship.cmd.v1"
                   :budget_ms 10
                   :work_max 280)
      (act select-action planner_action 2 action_cmd)
      (running))
    (seq
      (act select-action act_goal_direct 3 action_cmd)
      (running))))
```

Recommended planner state shape:

```text
[goal_dist, goal_bearing, obstacle_front, speed]
```

### checked-in shared source

Shared BT:

```lisp
--8<-- "examples/flagship_wheeled/lisp/bt_goal_flagship.lisp"
```

Shared helpers:

```lisp
--8<-- "examples/flagship_wheeled/lisp/contract_helpers.lisp"
```

Recommended comparison artefacts:

- canonical `mbt.evt.v1` log
- stable `active_branch` trace
- goal-distance-over-time plot
- action trace in the shared command intent
- success or timeout summary
- backend log normalisation with `examples/flagship_wheeled/tools/normalise_run.py`
- backend-neutral comparison with `examples/flagship_wheeled/tools/compare_runs.py`
- one fixed first-pass protocol for the aligned PyBullet/Webots prefix window

## example

Example backend split for the same shared BT:

- Webots:
  - observe e-puck state and proximity sensors
  - derive `goal_dist`, `goal_bearing`, and `obstacle_front`
  - convert `[linear_x, angular_z]` into wheel-speed output

- PyBullet:
  - observe racecar pose, speed, and rays
  - derive the same shared keys
  - convert `[linear_x, angular_z]` into steering and throttle

- ROS2:
  - observe odometry
  - derive pose, yaw, speed, goal error, and bounded obstacle context from the host side
  - publish `[linear_x, angular_z]` directly through `Twist`

## gotchas

- Do not choose line-following as the flagship. The line signal is too simulator-specific for the current ROS2 baseline.
- Do not choose wall-following as the flagship. The wall signal has the same problem.
- Do not keep random roam in the canonical tree. It weakens cross-transport comparison and makes failures harder to interpret.
- Do not force all backends to share one raw actuator shape. Share intent, not actuator internals.
- Do not broaden ROS2 topics or message contracts just to fit a simulator demo.
- Do not use the current Webots e-puck versus PyBullet racecar pair to argue for same-robot decision equivalence.

## see also

- [cross-transport shared contract](cross-transport-shared-contract.md)
- [same-robot strict comparison track](same-robot-strict-comparison.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
- [ROS2 backend scope](ros2-backend-scope.md)
- [cross-transport comparison protocol](cross-transport-comparison-protocol.md)
- [integration overview](overview.md)
- [Webots: e-puck goal seeking](../examples/webots-epuck-goal-seeking.md)
- [PyBullet: racecar](../examples/pybullet-racecar.md)
- [cross-transport flagship comparison](../examples/cross-transport-flagship-comparison.md)
