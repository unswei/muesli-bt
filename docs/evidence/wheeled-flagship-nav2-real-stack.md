# wheeled flagship nav2 real-stack evidence

!!! note "status"
    Status: experimental capture path.
    The checked-in fixture currently records `pending_real_stack_capture`. It does not yet claim a captured live Nav2 stack, simulator, map, lifecycle manager, or physical robot deployment.

## what this is

This page documents the real Nav2 stack evidence path for the experimental `wheeled-goal-flagship-nav-capability` variant.

The path runs the flagship BT through `cap.navigation.v1` and the optional ROS2/Nav2 adapter. It reaches a live `nav2_msgs/action/NavigateToPose` action server through the same capability dispatch path as Lisp `(cap.call request-map)`.

The fixture directory is:

```text
fixtures/ros2/wheeled_flagship_nav2_real_stack/
```

Until a Nav2 machine captures the run, the directory contains only a pending evidence manifest.

## when to use it

Use this path when you need release evidence that the wheeled flagship navigation-capability variant can talk to a configured Nav2 stack.

Use the fake-server evidence first when you only need to prove the ROS2 action-client boundary. Use this real-stack path when the Nav2 lifecycle manager, map, planner server, controller server, and simulator are actually running.

Do not use this page as physical robot evidence. The helper records `real_robot=false`.

## how it works

The ROS2-gated helper executable loads:

```lisp
--8<-- "examples/flagship_wheeled/lisp/bt_goal_flagship_nav_capability.lisp"
```

It then ticks `wheeled-goal-flagship-nav-capability` with scalar blackboard inputs for a navigation goal.

The `cap-navigation-tick` host action builds a `cap.navigation.request.v1` map. The registered Nav2 backend converts that request into a `NavigateToPose` goal and stores the returned job state on the blackboard.

The evidence runner captures two scenarios:

- `success`: submit a goal, poll status, and finish with `:ok`
- `cancel`: submit a goal, inject a collision branch, call cancel, and finish with `:cancelled`

Each captured scenario writes:

- `events.jsonl`
- `scenario_report.json`

The aggregate bundle writes:

- `evidence_manifest.json`
- `wheeled_flagship_nav2_real_stack_report.json`

The event logs use `mbt.evt.v1` only.

## api / syntax

No public Lisp syntax is added.

Build the helper only on a ROS2 machine:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF
cmake --build build/linux-ros2 -j --target wheeled_flagship_nav2_real_stack_evidence
```

Validate the checked-in bundle:

```bash
python3 tools/run_wheeled_flagship_nav2_real_stack_evidence.py --check
```

When the bundle still contains the pending marker, `--check` verifies that the marker does not claim real-stack evidence.

## example

The reviewed scenario defaults are:

```json
--8<-- "examples/nav2_real_stack/wheeled_flagship_scenario.json"
```

Start a Nav2 stack separately. Then capture the evidence:

```bash
python3 tools/run_wheeled_flagship_nav2_real_stack_evidence.py \
  --helper build/linux-ros2/wheeled_flagship_nav2_real_stack_evidence \
  --write \
  --ros-distro humble \
  --simulator external-nav2-stack \
  --goal-x 1.0 \
  --goal-y 0.0 \
  --goal-yaw 0.0 \
  --process-timeout-s 120
```

The command replaces the pending manifest with captured artefacts and validates the scenario event logs.

## gotchas

The helper does not launch Nav2. It expects a running action server at `--action-name`, which defaults to `/navigate_to_pose`.

The capture is simulator evidence unless a later, separate physical-run protocol marks it otherwise. Keep `real_robot=false` for simulator captures.

The canonical PyBullet, Webots, and ROS2 flagship wrappers still load `wheeled-goal-flagship`. This evidence path does not promote the navigation-capability variant into those wrappers.

If the action server is unavailable, the helper fails by default. That failure is useful: it prevents a missing Nav2 stack from being checked in as successful evidence.

The Python runner also applies an outer helper-process timeout. Use `--process-timeout-s` to tune that bound for slower simulators.

## see also

- [wheeled flagship navigation-capability evidence](wheeled-flagship-nav-capability.md)
- [Nav2 capability fake-server evidence](nav2-capability-fake-server.md)
- [cap.navigation.v1](../integration/cap-navigation-v1.md)
- [ROS2 backend scope](../integration/ros2-backend-scope.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
