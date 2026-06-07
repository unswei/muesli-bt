# wheeled flagship navigation-capability evidence

!!! note "status"
    Status: experimental evidence.
    This page documents the opt-in `wheeled-goal-flagship-nav-capability` variant. It does not promote the variant into the PyBullet, Webots, or ROS2 flagship wrappers, and it does not claim a live Nav2 stack, simulator, map, lifecycle manager, or physical robot deployment.

## what this is

The `wheeled-goal-flagship-nav-capability` variant delegates the flagship goal-seeking lane to `cap.navigation.v1`.

The canonical shared flagship remains `wheeled-goal-flagship`. Existing backend wrappers still load that shared tree.

The checked-in artefacts live under:

```text
fixtures/dsl/wheeled_flagship_nav_capability/
```

The bundle contains:

- `evidence_manifest.json`
- `wheeled_flagship_nav_capability_report.json`
- representative `events.jsonl` files for accepted/success, rejected, timeout, and cancel-on-collision paths

## when to use it

Use this evidence when you need to check whether the wheeled flagship can route goal navigation through the generic navigation capability contract.

Do not use this evidence as proof of a real Nav2 deployment. The core bundle uses the deterministic `mock-nav2` adapter. ROS2 builds add fake-action-server unit coverage for the same BT variant and the real Nav2 action-client adapter. Real-stack capture uses the separate [wheeled flagship Nav2 real-stack evidence](wheeled-flagship-nav2-real-stack.md) path.

## how it works

The variant has three branches:

1. goal reached
2. collision recovery
3. navigation capability

The collision branch cancels an active navigation job before selecting the fixed avoid command. The navigation branch uses `cap-navigation-tick`, which builds a `cap.navigation.request.v1` map from scalar blackboard keys and calls the same internal path as Lisp `(cap.call request-map)`.

The core evidence checker regenerates the fixture bundle by ticking the BT through `muslisp`, extracting representative `cap_call_start` and `cap_call_end` records, validating the event shape, and comparing the result with checked-in artefacts.

## api / syntax

Load the variant explicitly:

```lisp
(load "examples/flagship_wheeled/lisp/bt_goal_flagship_nav_capability.lisp")
(define inst (bt.new-instance wheeled-goal-flagship-nav-capability))
```

Full source:

```lisp
--8<-- "examples/flagship_wheeled/lisp/bt_goal_flagship_nav_capability.lisp"
```

Required blackboard inputs for submit:

- `nav_goal_x`
- `nav_goal_y`

Optional blackboard inputs:

- `nav_goal_frame`, default `"map"`
- `nav_goal_yaw`, default `0.0`
- `nav_timeout_ms`, default `1000`
- `nav_action_name`, used by ROS2 fake-server tests and non-default Nav2 action names
- `nav_mock_status`, used by deterministic core mock fixtures

The action writes:

- `nav_status`
- `nav_job_id`
- `nav_request_hash`
- `nav_response_hash`
- `nav_host_reached`
- `nav_distance_remaining_m`
- `nav_number_of_recoveries`
- `nav_navigation_time_ms`
- `nav_estimated_time_remaining_ms`
- `active_branch`

## example

Regenerate and compare the core evidence bundle:

```bash
cmake --build --preset core-only -j
python3 tools/run_wheeled_flagship_nav_capability_evidence.py \
  --muslisp build/core-only/muslisp \
  --check
```

On a ROS2 Humble machine, run the ROS2-gated CTest path to exercise the same variant against the in-process fake `NavigateToPose` server:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF
cmake --build build/linux-ros2 -j
ctest --test-dir build/linux-ros2 --output-on-failure \
  -R "ros2 wheeled flagship navigation capability fake server"
```

## gotchas

The variant is evidence infrastructure. It is not the default flagship.

The core artefacts use `mock-nav2`. They prove the BT-to-capability contract and canonical event shape, not ROS2 transport.

The ROS2-gated unit test proves the fake-action-server boundary. It still does not prove a configured Nav2 lifecycle stack, map server, planner server, simulator, or physical robot.

The real-stack evidence path is separate and currently checked in as pending capture until a Nav2 machine regenerates the artefacts.

## see also

- [cap.navigation.v1](../integration/cap-navigation-v1.md)
- [Nav2 capability fake-server evidence](nav2-capability-fake-server.md)
- [wheeled flagship Nav2 real-stack evidence](wheeled-flagship-nav2-real-stack.md)
- [ROS2 backend scope](../integration/ros2-backend-scope.md)
- [cross-transport flagship](../integration/cross-transport-flagship.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
