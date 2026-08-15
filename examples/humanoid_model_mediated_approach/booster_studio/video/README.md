# T1 paper-video prototype

## what this is

This directory produces the polished 10–12 second T1 simulation shot. The shot
shows one Booster K1, one stationary ball, the delayed request, acceptance of a
current result and visible walking towards the accepted target.

The renderer reads the canonical `mbt.evt.v1` stream. It refuses a run unless
there is exactly one invocation-correlated submission, accepted result and
accepted walking-target dispatch. The target marker and captions therefore
describe the recorded run rather than an independently scripted outcome.

## when to use it

Use this shot as the visual-quality gate before recording T2 and T3. An
uninformed viewer should be able to identify the request, accepted target and
robot motion without narration.

Keep the SDK-independent overlay and raw video in the evidence bundle. The
large paper-video captions are an editorial rendering of the same evidence;
they do not replace `mbt.evt.v1`.

## how it works

`t1_prototype.json` freezes:

- an 11-second shot with three seconds before request submission;
- the K1 and ball A simulation poses;
- a 16:9 crop and 1080p output;
- fixed-camera pixel calibration for the robot, ball and current target; and
- the clean capture framing.

`stage_t1_scene.py` resets robot motion control before positioning robot1 and
ball A. `capture_clean_simulator.py` temporarily hides the HTML scoreboard and
referee feed through Chromium's DevTools Protocol. It does not modify physics
or the simulator package, and it restores the UI after capture.

`render_t1_prototype.py` aligns video time from the capture clock and the
canonical `vla_submit` clock. It derives the field target by adding the
ball-relative accepted target to the captured ball position. The fixed-camera
pixel point is an explicit, hashed rendering calibration. The render manifest
records both the derived metric target and the calibrated screen point.

## api / syntax

Stage the scene inside the virtual K1 container, where the physics websocket is
available:

```bash
python3 video/stage_t1_scene.py
```

Arm motion only after the bridge reports a fresh ball, robot pose and stable
state:

```bash
ros2 topic pub /muesli/emergency std_msgs/msg/Bool '{data: false}' \
  --rate 10 --times 5
ros2 topic pub /muesli/motion_arm std_msgs/msg/Bool '{data: true}' \
  --rate 10 --times 5
```

Run the clean capture on the Studio host. Start T1 about four seconds after
`CAPTURE_READY` so the recording includes the configured three-second lead:

```bash
python3 video/capture_clean_simulator.py \
  --output-dir /tmp/t1-clean-capture \
  --duration 15
```

Start the trial in the virtual K1 container:

```bash
ros2 topic pub --once /muesli/trial_command \
  std_msgs/msg/String '{data: T1}'
```

Render after copying the completed live run out of the container:

```bash
python3 video/render_t1_prototype.py \
  --events /tmp/t1-live-run/events.jsonl \
  --capture-timing /tmp/t1-clean-capture/capture-timing.json \
  --raw-video /tmp/t1-clean-capture/capture-full.mp4 \
  --output-dir /tmp/t1-polished
```

The output directory contains `t1-polished-prototype.mp4`, the generated ASS
overlay and a hash-bound `render-manifest.json`.

## example

The accepted-result transition occurs at 5.5 seconds in the frozen shot:

```text
0.0 s  T1 normal-result setup
3.0 s  model request pending; generation and captured context visible
5.5 s  current result accepted; green target appears
6.5 s  robot visibly walks towards the target
11 s   shot ends after arrival
```

The paper-video acceptance gate is:

- clean field view with no scoreboard or referee feed;
- readable request and result cards at 1080p;
- green current target visible for at least two seconds;
- robot displacement greater than 0.5 m; and
- one accepted dispatch in the canonical event stream.

## gotchas

- The capture host needs `websocket-client`. The virtual K1 staging helper uses
  the separate `websockets` package supplied by Booster Studio.
- The renderer needs an `ffmpeg` build with the `ass` filter from libass.
- The pixel calibration is valid only for the frozen camera and crop. Reframe
  the camera only after updating the calibration and reviewing a new marker
  alignment frame.
- The letters `A` and `B` are video labels. The actual monotonic context ID is
  also displayed and retained in the render manifest.
- Disarm motion after every take. Disarming clears the target and closes the
  Booster backend.

## see also

- [T2 moved-ball paper-video comparison](t2-comparison.md)
- [Booster Studio host adapter](../README.md)
- [humanoid video experiment](../../README.md)
- [experiment contract](../../../../docs/project/humanoid-model-mediated-approach-contract.md)
