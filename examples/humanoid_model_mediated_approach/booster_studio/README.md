# Booster Studio host adapter

Status: the Studio package source, native-runner supervisor and video-overlay
path are locally integrated and offline tested. The signed Agent installs and
starts on a virtual K1 with the pinned Linux payload. At source commit
`e88bdaa`, motion-enabled T1, T2a, T2b and T3 rehearsals completed in Booster
Studio with canonical evidence. Motion remains disabled by default. Physical
capture remains pending.

## what this is

This directory is a Booster Studio agent project for the humanoid approach
experiment. It supplies the Booster-owned half of the host boundary:

- fresh ball observations and monotonic ball context IDs;
- robot pose, stability and software emergency state;
- a synchronous local dispatch gate;
- conversion from a ball-relative approach pose to a field target;
- a bounded field-target follower that emits body-frame velocity commands;
- a manifest-verified Linux C++ trial runner; and
- canonical-event-derived video overlays and evidence manifests.

The adapter does not implement invocation authority. The muesli C++ runtime
retains generation, branch, deadline, context and exactly-once authority. The
adapter accepts only a target that has already passed that runtime gate, then
rechecks live Booster state before admitting it to the walking controller.

## when to use it

Use this project after the SDK-independent trial matrix and local bridge tests
pass. Start in the Booster `football3v3` scene with one K1. Do not enable motion
until the snapshot/dispatch round trip passes in that scene.

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
connection. The C++ `bridge_walking_target_dispatcher` uses the response as its
synchronous host decision so the canonical `walking_target_dispatch` event
records the actual Booster acceptance or rejection.

The agent waits for a fresh ball, fresh robot pose and stable robot state before
starting a requested native trial. It verifies the Linux executable and every
frozen BT, configuration and evidence protocol against
`res/native_payload/manifest.json`.
A changed, missing, symlinked or wrong-architecture file prevents launch.

The native process is supervised. Agent shutdown terminates the process and
clears the walking target. An unexpected non-zero exit clears motion and
latches the software emergency state. A successful run writes
`events.jsonl`, `overlay.ass` and `live-manifest.json` under the evidence root.
The ASS subtitle overlay is derived from `mbt.evt.v1`; it is not another event
log.

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
| `MUESLI_BOOSTER_NATIVE_PAYLOAD_ROOT` | packaged `res/native_payload/` | Verified native payload root. |
| `MUESLI_BOOSTER_EVIDENCE_ROOT` | `/tmp/muesli-humanoid-runs` | Live trial evidence directory. |
| `MUESLI_BOOSTER_AUTOSTART_TRIAL` | empty | Start `T1`, `T2a`, `T2b` or `T3` once the host is ready. |
| `MUESLI_BOOSTER_TRIAL_STARTUP_TIMEOUT_S` | `30` | Maximum wait for fresh ball, pose and stability. |

Studio exposes six operator actions after the Agent starts:

| Action ID | Effect |
| --- | --- |
| `motion_arm` | Toggle the Booster walking backend. Disarming stops any trial and clears its walking target. |
| `trial_t1` | Start the normal full-authority trial. |
| `trial_t2a` | Start the moved-ball timeout-only baseline. |
| `trial_t2b` | Start the moved-ball invocation-scoped trial. |
| `trial_t3` | Start the higher-priority interruption trial. |
| `software_emergency` | Toggle the controlled emergency used after the T3 request cue. |

The Agent always starts fail-closed unless
`MUESLI_BOOSTER_MOTION_ENABLED=true` is supplied by a controlled deployment.
For interactive Studio work, use `motion_arm` so arming is a visible operator
action. A trial action refuses to launch until motion is armed and the ball,
robot pose and stability observations are fresh.

The `football3v3` match runner starts team processes outside Agent Manager's
normal active-Agent route. Studio `1.110.1` therefore cannot deliver component
clicks to a running team Agent. Use the equivalent ROS 2 control topics in that
scene:

| Topic | Type | Meaning |
| --- | --- | --- |
| `/muesli/motion_arm` | `std_msgs/msg/Bool` | `true` arms motion; `false` stops the trial, clears the target and closes the backend. |
| `/muesli/trial_command` | `std_msgs/msg/String` | Start exactly one of `T1`, `T2a`, `T2b` or `T3`. |
| `/muesli/emergency` | `std_msgs/msg/Bool` | Set or clear the controlled emergency state. |

## example

Run the adapter policy and socket tests without ROS, BoosterOS or a simulator:

```bash
python3 -m unittest discover \
  -s examples/humanoid_model_mediated_approach/booster_studio/tests \
  -p 'test_*.py' -v
```

Run the C++ client, Python adapter and end-to-end native-runner checks:

```bash
ctest --test-dir build/dev --output-on-failure \
  -R '^muesli_bt_booster_(bridge|bridge_runner|studio_adapter)$'
```

Prepare the Linux x86-64 payload with a working Docker Buildx builder:

```bash
python3 examples/humanoid_model_mediated_approach/booster_studio/tools/build_native_payload.py
python3 examples/humanoid_model_mediated_approach/booster_studio/tools/build_native_payload.py \
  --check-only
```

The build uses a digest-pinned Ubuntu 22.04 image and links the GNU C++ runtime
libraries into the runner. `build.toml` advertises only `sim_x86_64` until
separate ARM and device payloads are built and tested.

The publisher first copies temporary build output beside
`res/native_payload/`, then uses same-filesystem renames for the managed
entries. The build therefore works when the system temporary directory and
repository are on different filesystems. Booster Studio includes `res/` in the
signed Agent, so the verifier can find the payload both in a checkout and after
installation.

For an unattended virtual K1 trial, set motion and one trial explicitly in the
Studio Agent environment:

```text
MUESLI_BOOSTER_MOTION_ENABLED=true
MUESLI_BOOSTER_AUTOSTART_TRIAL=T2b
```

For an interactive trial in ordinary Agent mode, start recording, activate the
Agent, invoke `motion_arm`, then invoke one trial action. In `football3v3`,
publish the corresponding ROS commands instead:

```bash
ros2 topic pub --once /muesli/motion_arm std_msgs/msg/Bool '{data: true}'
ros2 topic pub --once /muesli/trial_command std_msgs/msg/String '{data: T2b}'
```

Move the ball after the `REQUEST_SUBMITTED` cue for T2a or T2b. For T3,
publish the controlled software emergency after the cue. Run one trial at a
time and retain its evidence directory.

```bash
ros2 topic pub --once /muesli/emergency std_msgs/msg/Bool '{data: true}'
```

After recording, note the request cue time in the raw clip and render the
aligned overlay:

```bash
python3 examples/humanoid_model_mediated_approach/booster_studio/tools/finalise_video_evidence.py \
  --run-dir /tmp/muesli-humanoid-runs/<run-id> \
  --raw-video /path/to/recording.mp4 \
  --request-cue-seconds 4.20
```

This requires `ffmpeg`. The finaliser retains the raw clip, writes
`overlay-video.mp4`, and records hashes and the cue alignment in
`live-manifest.json`.

For the paper video, keep the hardware shot to one K1 and one ball. Simulation
can carry the richer comparison: use a fixed camera, park irrelevant robots,
show T2a and T2b side by side, and add field markers for the old target and the
current target. The canonical overlay reports runtime result and host dispatch
as separate decisions so the timeout-only baseline is not ambiguous.

Open `booster_studio/` as a Booster Studio project only after the offline test
passes. The project metadata selects `football3v3` and `soccer-match`.

## gotchas

- Motion defaults to disabled and a dispatch then returns `motion_disabled`.
- The `motion_arm` action is an explicit safety boundary. Triggering a trial
  action while disarmed returns `not ready` and launches no native process.
- Disarming stops the active native process, revokes its walking target and
  commands zero velocity before closing the Booster backend.
- Autostart is empty by default. Setting a trial ID does nothing until the ball,
  pose and stability snapshot is fresh.
- A live trial needs motion enabled even for T2a, because the experiment must
  show that the host blocks the stale target for `context_changed`, not merely
  because all motion was disabled.
- The adapter creates no second event log. `events.jsonl` from muesli remains
  the sole external runtime evidence stream.
- The ball context frame is field-aligned in the frozen experiment contract.
  Conversion therefore adds the ball translation and does not rotate offsets.
- A context change, stale ball, stale robot pose, instability or emergency
  clears the active target and outputs zero velocity.
- A successful T1 runner exit leaves its already-authorised target with the
  bounded host follower. The follower stops at arrival or immediately on any
  context, observation, stability or emergency failure. Agent shutdown also
  clears it.
- The C++ and Python bridge, supervisor, payload verifier and overlay generator
  have passed local tests. The full live matrix passed on a virtual K1 at
  source commit `e88bdaa`; re-run the matrix and retain fresh evidence after a
  behavioural change.
- An Apple Silicon build of the C++ runner is Mach-O/ARM and is deliberately
  rejected. Use the pinned container build for `sim_x86_64`.
- The `football3v3` scene includes other robots and referee state. Freeze or
  park irrelevant actors before recording the one-robot paper trial.

## see also

- [experiment directory README](../README.md)
- [experiment contract](../../../../docs/project/humanoid-model-mediated-approach-contract.md)
- [approach-pose validation](../../../../docs/bt/approach-pose-validation.md)
- [Booster Studio bridge](../../../../docs/integration/booster-studio-bridge.md)
