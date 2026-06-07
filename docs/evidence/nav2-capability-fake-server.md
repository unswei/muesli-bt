# nav2 capability fake-server evidence

!!! note "status"
    Status: experimental ROS2-gated evidence.
    This page documents fake-action-server evidence for the optional `cap.navigation.v1` Nav2 adapter. It does not claim a configured Nav2 stack, simulator, map, lifecycle manager, or physical robot deployment.

## what this is

This evidence bundle proves the first real ROS2 action-client boundary for `cap.navigation.v1`.

The helper executable runs deterministic `nav2_msgs/action/NavigateToPose` scenarios against an in-process fake action server. Each scenario is driven through Lisp `(cap.call request-map)`, not by calling the C++ adapter directly.

The checked-in artefacts live under:

```text
fixtures/ros2/nav2_capability_fake_server/
```

The bundle contains:

- `evidence_manifest.json`
- `nav2_capability_report.json`
- one `events.jsonl` file per scenario

## when to use it

Use this bundle when you need to check whether the optional ROS2/Nav2 adapter still:

- accepts navigation requests through `cap.navigation.v1`
- reaches the ROS2 `NavigateToPose` action-client boundary
- maps feedback, success, rejection, abort, cancellation, unavailable server, and timeout outcomes into stable capability results
- emits canonical `mbt.evt.v1` `cap_call_start` and `cap_call_end` events

Do not use this bundle as evidence that a real robot, simulator, map server, controller server, planner server, or Nav2 lifecycle stack works.

## how it works

The ROS2-gated helper binary is built only when `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON`.

The helper starts fake action servers for the server-backed scenarios, creates a normal muslisp environment with the ROS2 extension registered, and evaluates Lisp request maps through `(cap.call request-map)`.

The checked scenarios are:

- `accepted_success`
- `rejected`
- `abort_error`
- `cancelled`
- `unavailable`
- `timeout`

The report records scenario name, request operation, statuses, host reach, job id, request and response hashes, progress summary, fake-server goal and cancel counts, and the received pose summary when a fake server receives a goal.

The per-scenario event logs are canonical `mbt.evt.v1` JSONL files. They contain the representative capability-call events for that scenario.

## api / syntax

The helper uses the public navigation capability request shape:

```lisp
(begin
  (define target (map.make))
  (map.set! target 'frame "map")
  (map.set! target 'x 1.25)
  (map.set! target 'y -0.5)
  (map.set! target 'yaw 0.5)

  (define req (map.make))
  (map.set! req 'schema_version "cap.navigation.request.v1")
  (map.set! req 'capability "cap.navigation.v1")
  (map.set! req 'operation "navigate-to-pose")
  (map.set! req 'request_id "nav2-evidence-accepted-success")
  (map.set! req 'action_name "/muesli_bt_nav2_evidence/accepted_success/navigate_to_pose")
  (map.set! req 'target target)
  (map.set! req 'timeout_ms 500)
  (cap.call req))
```

The adapter metadata reports `adapter="nav2"` in result maps and capability-call events.

## example

On a ROS2 Humble machine with `ros-humble-nav2-msgs` installed:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF
cmake --build build/linux-ros2 -j --target nav2_capability_evidence
python3 tools/run_nav2_capability_evidence.py \
  --helper build/linux-ros2/nav2_capability_evidence \
  --check
```

To regenerate the checked-in artefacts after an intentional behaviour change:

```bash
python3 tools/run_nav2_capability_evidence.py \
  --helper build/linux-ros2/nav2_capability_evidence \
  --write
```

`--check` regenerates into a temporary directory, validates each event log with `tools/validate_log.py`, and compares the output against the checked-in fixture bundle.

## gotchas

Core-only builds do not build the helper and do not require ROS2, Nav2, or `nav2_msgs`.

The fake server is test and evidence infrastructure. It is not installed as a public adapter.

The timeout scenario proves bounded action-client behaviour and stable event/report output. It does not prove Nav2 planner timing.

The event logs are representative scenario logs, not a full ROS bag, lifecycle trace, or physical-run record.

## see also

- [cap.navigation.v1](../integration/cap-navigation-v1.md)
- [ROS2 backend scope](../integration/ros2-backend-scope.md)
- [host capability bundles](../integration/host-capability-bundles.md)
- [canonical event log](../observability/event-log.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
