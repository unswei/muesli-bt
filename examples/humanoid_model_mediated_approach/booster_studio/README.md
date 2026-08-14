# Booster Studio host adapter

Status: offline-tested integration scaffold. Motion is disabled by default. The
live C++ bridge client, Studio package build and virtual K1 trial remain pending.

## what this is

This directory is a Booster Studio agent project for the humanoid approach
experiment. It supplies the Booster-owned half of the host boundary:

- fresh ball observations and monotonic ball context IDs;
- robot pose, stability and software emergency state;
- a synchronous local dispatch gate;
- conversion from a ball-relative approach pose to a field target; and
- a bounded field-target follower that emits body-frame velocity commands.

The adapter does not implement invocation authority. The muesli C++ runtime
retains generation, branch, deadline, context and exactly-once authority. The
adapter accepts only a target that has already passed that runtime gate, then
rechecks live Booster state before admitting it to the walking controller.

## when to use it

Use this project after the SDK-independent trial matrix passes. Start in the
Booster `football3v3` scene with one K1. Do not enable motion until the muesli
C++ bridge client is connected and the snapshot/dispatch round trip passes.

## how it works

The known Booster interfaces are isolated in `runtime.py`. ROS 2 supplies the
simulation ground truth:

- `/team1/soccer/sim/ground_truth/ball`;
- `/team1/robot1/soccer/sim/ground_truth/robot_pose`; and
- `/muesli/emergency` for the controlled interruption.

The robot backend uses `BoosterRobot`, the `soccer` gait, `walk` mode,
`get_fall_down_state()` and `set_velocity()`. No robot object is created while
motion is disabled.

The platform-independent adapter serves one JSON request per Unix-domain socket
connection. A future C++ `walking_target_dispatcher` must use the response as
its synchronous host decision so the canonical `walking_target_dispatch` event
records the actual Booster acceptance or rejection.

## api / syntax

The socket defaults to `/tmp/muesli-booster-bridge.sock`. Override it with
`MUESLI_BOOSTER_BRIDGE_SOCKET`.

Snapshot request:

```json
{"op":"snapshot"}
```

Dispatch request:

```json
{
  "op": "dispatch",
  "schema_version": "humanoid.booster_dispatch_request.v1",
  "job_id": "job-42",
  "generation": 7,
  "captured_context_id": "ball-0001",
  "target": {
    "frame_id": "ball_context",
    "x_m": -0.45,
    "y_m": 0.08,
    "yaw_rad": 0.0
  }
}
```

The response uses `humanoid.booster_dispatch_response.v1` and returns
`accepted`, `reason` and the transformed field target. Known rejection reasons
include `motion_disabled`, `robot_unstable`, `ball_stale`, `context_changed`,
`duplicate_dispatch`, `invalid_frame`, `invalid_pose` and
`outside_operating_area`.

Runtime settings:

| Environment variable | Default | Meaning |
| --- | --- | --- |
| `MUESLI_BOOSTER_MOTION_ENABLED` | `false` | Permit creation of the robot backend and velocity output. |
| `MUESLI_BOOSTER_TEAM_ID` | `1` | Team namespace used by simulation topics. |
| `MUESLI_BOOSTER_ROBOT_NAME` | `robot1` | Controlled virtual robot name. |
| `MUESLI_BOOSTER_CONTROL_HZ` | `20` | Walking follower rate. |
| `MUESLI_BOOSTER_CONTEXT_THRESHOLD_M` | `0.15` | Ball displacement that creates a new context. |
| `MUESLI_BOOSTER_BALL_MAX_AGE_S` | `0.5` | Maximum ball observation age. |
| `MUESLI_BOOSTER_ROBOT_POSE_MAX_AGE_S` | `0.5` | Maximum robot pose age. |
| `MUESLI_BOOSTER_MIN_FIELD_X_M` / `MAX_FIELD_X_M` | `-6.5` / `6.5` | Permitted field x range. |
| `MUESLI_BOOSTER_MIN_FIELD_Y_M` / `MAX_FIELD_Y_M` | `-4.0` / `4.0` | Permitted field y range. |

## example

Run the adapter policy and socket tests without ROS, BoosterOS or a simulator:

```bash
python3 -m unittest discover \
  -s examples/humanoid_model_mediated_approach/booster_studio/tests \
  -p 'test_*.py' -v
```

Open `booster_studio/` as a Booster Studio project only after the offline test
passes. The project metadata selects `football3v3` and `soccer-match`.

## gotchas

- Motion defaults to disabled and a dispatch then returns `motion_disabled`.
- The adapter creates no second event log. `events.jsonl` from muesli remains
  the sole external runtime evidence stream.
- The ball context frame is field-aligned in the frozen experiment contract.
  Conversion therefore adds the ball translation and does not rotate offsets.
- A context change, stale ball, stale robot pose, instability or emergency
  clears the active target and outputs zero velocity.
- The current bridge server is ready, but no C++ client invokes it yet. Do not
  claim a complete end-to-end Booster run until that client is implemented.
- The `football3v3` scene includes other robots and referee state. Freeze or
  park irrelevant actors before recording the one-robot paper trial.

## see also

- [experiment directory README](../README.md)
- [experiment contract](../../../../docs/project/humanoid-model-mediated-approach-contract.md)
- [approach-pose validation](../../../../docs/bt/approach-pose-validation.md)
