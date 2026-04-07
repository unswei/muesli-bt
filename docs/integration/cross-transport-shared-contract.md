# cross-transport shared contract

## what this is

This page defines the shared blackboard and command contract for the `v0.5.0` cross-transport wheeled flagship.

This contract is narrower than the full runtime contract.
It exists so one high-level BT can run across:

- Webots
- PyBullet
- ROS2

while each [host](../terminology.md#host) (backend) keeps its own sensing and actuation details.

This page is a design-level specification for the `v0.5.0` flagship.
It is not a new external runtime schema and it does not change BT or Lisp semantics.

## when to use it

Use this page when you:

- implement the shared flagship BT
- map backend observations into the shared blackboard keys
- convert shared command intent into backend-native actuator commands
- review whether a proposed comparison still fits the released ROS2 `Odometry` -> `Twist` boundary

## how it works

The contract has two layers:

1. a shared blackboard state that the flagship BT reads
2. a shared command intent that the flagship BT writes

The shared BT should depend only on this contract.
Backends may expose richer observations internally, but those richer observations should be normalised into this smaller shared surface before the BT consumes them.

### coordinate and unit rules

Use these conventions:

- `pose_x`, `pose_y`, `goal_dist`: metres
- `yaw`, `goal_bearing`: radians
- `speed`: metres per second
- `obstacle_front`: unitless normalised risk in `[0.0, 1.0]`
- shared command intent: normalised unitless command in `[-1.0, 1.0]`

Angle rules:

- `yaw` should be wrapped to `[-pi, pi]`
- `goal_bearing` should be the wrapped signed heading error to the goal in `[-pi, pi]`

### derivation rules

Backends should derive the shared keys with the same high-level meaning.

Reference derivations:

- `goal_dist = hypot(goal_x - pose_x, goal_y - pose_y)`
- `goal_bearing = wrap(atan2(goal_y - pose_y, goal_x - pose_x) - yaw)`
- `goal_reached = (goal_dist <= goal_tolerance_m)`
- `collision_imminent = (obstacle_front >= collision_threshold)`

`goal_tolerance_m` and `collision_threshold` may vary slightly by scenario, but they must be documented with the example and kept fixed for a given comparison run.

For the current PyBullet/Webots flagship comparison, the provisional checked-in values are:

- `goal_tolerance_m = 0.25`
- `collision_threshold = 0.75`

### backend split

Shared BT responsibilities:

- read the shared blackboard keys
- choose between succeed, avoid, plan, and deterministic direct-to-goal branches
- write one shared command intent

Backend responsibilities:

- observe native simulator or ROS data
- derive the shared blackboard keys
- convert shared command intent into backend-native commands
- enforce backend-side safe fallback if command application fails

## api / syntax

### required shared blackboard keys

`goal_reached`
: boolean
: `#t` when the configured goal has been reached under the scenario tolerance

`collision_imminent`
: boolean
: `#t` when front obstacle risk is high enough that the avoid branch should win

`goal_dist`
: finite number
: distance to the active goal in metres
: required range: `>= 0.0`

`goal_bearing`
: finite number
: wrapped signed heading error to the goal in radians
: required range: `[-pi, pi]`

`pose_x`
: finite number
: current x position in metres

`pose_y`
: finite number
: current y position in metres

`yaw`
: finite number
: wrapped heading in radians
: required range: `[-pi, pi]`

`speed`
: finite number
: current forward speed in metres per second

`obstacle_front`
: finite number
: normalised front obstacle risk
: required range: `[0.0, 1.0]`

`planner_state`
: numeric vector
: canonical planner input for the flagship
: required shape:

```text
[goal_dist, goal_bearing, obstacle_front, speed]
```

`act_avoid`
: numeric vector
: deterministic avoid command in the shared command intent
: required shape:

```text
[linear_x, angular_z]
```

`act_goal_direct`
: numeric vector
: deterministic direct-to-goal fallback command in the shared command intent
: required shape:

```text
[linear_x, angular_z]
```

### recommended shared blackboard outputs

`planner_action`
: numeric vector
: planner-selected command in the shared command intent

`planner_meta`
: map or JSON-encoded map
: planner diagnostics already used by current examples

`action_cmd`
: numeric vector
: final selected command in the shared command intent

`active_branch`
: integer
: stable branch id for comparison and plotting

Recommended branch ids:

- `1`: avoid
- `2`: planner
- `3`: direct goal fallback

### shared command intent

The canonical command intent for the flagship is:

```text
[linear_x, angular_z]
```

Field rules:

- `linear_x`: normalised forward command in `[-1.0, 1.0]`
- `angular_z`: normalised turn command in `[-1.0, 1.0]`

Interpretation rules:

- `0.0` means neutral command
- positive `linear_x` means forward motion intent
- positive `angular_z` means left-turn intent
- negative values are allowed when a backend or avoid policy needs reverse or right-turn behaviour

Mapping rules:

- ROS2 publishes the values through `ros2.action.v1` and `geometry_msgs/msg/Twist`
- Webots converts the values into left and right wheel commands
- PyBullet converts the values into steering and throttle

The shared BT should never write wheel-speed pairs or steering/throttle pairs directly.

Recommended planner binding for the shared flagship:

- `model_service`: `flagship-goal-shared-v1`
- `action_schema`: `flagship.cmd.v1`

### reference BT shape

The flagship BT should fit this branch order:

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
                   :model_service "flagship-goal-shared-v1"
                   :state_key planner_state
                   :action_key planner_action
                   :meta_key planner_meta)
      (act select-action planner_action 2 action_cmd)
      (running))
    (seq
      (act select-action act_goal_direct 3 action_cmd)
      (running))))
```

## example

Example backend mappings:

- Webots cluttered goal:
  - derive `goal_dist` and `goal_bearing` from robot pose and goal marker pose
  - derive `obstacle_front` from front proximity channels
  - map `[linear_x, angular_z]` to wheel-speed output

- PyBullet racecar:
  - derive `goal_dist` and `goal_bearing` from `x`, `y`, `yaw`, and goal coordinates
  - derive `obstacle_front` from forward ray distances
  - map `[linear_x, angular_z]` to throttle and steering

- ROS2:
  - derive `pose_x`, `pose_y`, `yaw`, and `speed` from odometry
  - derive `goal_dist`, `goal_bearing`, and `obstacle_front` in the host layer from configured goal state and bounded obstacle context
  - publish `[linear_x, angular_z]` directly through `Twist`

## gotchas

- Do not let backend-native sensor shapes leak into the shared BT.
- Do not let backend-native actuator shapes leak into the shared BT either.
- Do not mix physical actuator units with the shared normalised command intent.
- Do not leave `goal_tolerance_m` or `collision_threshold` implicit. Record them with the example or comparison script.
- Do not treat missing keys as harmless. If a required shared key is absent, fail early and log the problem clearly.

## see also

- [cross-transport flagship for v0.5](cross-transport-flagship.md)
- [sensing and blackboard](sensing-and-blackboard.md)
- [environment api (`env.api.v1`)](env-api.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
