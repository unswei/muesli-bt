# T2 moved-ball paper-video comparison

## what this is

This workflow produces the polished 20-second T2 simulation comparison. The
left panel is the timeout-only T2a baseline. The right panel is the full
invocation-scoped T2b system. Both panels begin with the same Booster K1, ball
pose and camera view.

The ball moves conspicuously from A to B while the same deterministic
2.5-second model request is running. The baseline runtime accepts the obsolete
result and the independent Booster host gate blocks it. The invocation-scoped
runtime rejects the obsolete result before dispatch. Both panels then request
a pose for current ball B, accept it and visibly walk towards the current
target. No command is sent towards the obsolete target for A.

## when to use it

Use this comparison after the T1 visual-quality gate passes. Use matched fresh
captures rather than duplicating one clip because the video must show that both
policies ran in the simulator.

Retain both raw videos, the four canonical `mbt.evt.v1` streams, both capture
clocks and the render manifest. Each continuous capture contains one moved-ball
trial followed by one normal current-B recovery trial. The editorial overlay
does not replace those evidence artefacts.

## how it works

`t2_comparison.json` freezes:

- a 20-second comparison with three seconds before each moved-ball request;
- equal 960-pixel panels in a 1080p output;
- identical K1 and ball A starting poses;
- ball B at `(1.1, 0.65)` metres in the field frame; and
- fixed-camera panel calibration for A, B, the obsolete target and the current
  B target.

The trial controller moves the ball one second after `REQUEST_SUBMITTED`.
After the first trial completes, the controller submits a normal-result trial
without resetting the scene. That recovery request therefore captures the
current B context. `render_t2_comparison.py` independently aligns each clean
capture to its first canonical `vla_submit` event. It refuses to render unless
all of these claims are present in the four event streams:

- T2a uses `deadline_only`, accepts the stale result, then records one rejected
  `walking_target_dispatch` with reason `context_changed`;
- T2b uses `invocation_scoped`, rejects with reason `context_changed`, and
  records no `walking_target_dispatch` event;
- both invocations correlate generation, job and captured context;
- both trials record the same ball A, ball B and candidate target; and
- both moved-ball `run_end` events report zero backend dispatch calls;
- each recovery captures the preceding current B context and records the same
  ball B state and ball-relative candidate; and
- each recovery accepts one current result and records exactly one accepted
  walking backend call.

The obsolete field target is derived from captured ball A. The recovery target
is derived from current ball B. Red and green screen points use the explicit,
hash-bound fixed-camera calibration.

## api / syntax

Stage the common scene inside the virtual K1 container. The T1 staging helper
accepts the T2 comparison configuration because both use the same paper-video
scene fields:

```bash
python3 video/stage_t1_scene.py --shot video/t2_comparison.json
```

Arm motion only after the bridge reports fresh, stable state. Start a clean
24-second capture about four seconds before each moved-ball trial. Run T2a and
T2b as separate takes, resetting the scene between them:

```bash
python3 video/capture_clean_simulator.py \
  --output-dir /tmp/t2a-clean --duration 24

ros2 topic pub --once /muesli/trial_command \
  std_msgs/msg/String '{data: T2a}'

# After T2a completes, keep ball B and the same continuous capture.
ros2 topic pub --once /muesli/trial_command \
  std_msgs/msg/String '{data: T1}'
```

Move ball body 141 from A to B one second after the first request cue. Start the
T1 recovery about one second after T2a completes. Repeat the sequence with
`T2b`, the same positions and a new clean capture. The automated simulation
controller may perform the move and both trial submissions.

Render the matched captures on a host whose `ffmpeg` includes libass:

```bash
python3 video/render_t2_comparison.py \
  --baseline-events /tmp/t2a-run/events.jsonl \
  --baseline-recovery-events /tmp/t2a-recovery/events.jsonl \
  --baseline-capture-timing /tmp/t2a-clean/capture-timing.json \
  --baseline-raw-video /tmp/t2a-clean/capture-full.mp4 \
  --full-events /tmp/t2b-run/events.jsonl \
  --full-recovery-events /tmp/t2b-recovery/events.jsonl \
  --full-capture-timing /tmp/t2b-clean/capture-timing.json \
  --full-raw-video /tmp/t2b-clean/capture-full.mp4 \
  --output-dir /tmp/t2-polished
```

The output directory contains `t2-polished-comparison.mp4`, the generated ASS
overlay and `render-manifest.json` with hashes of every input and output.

## example

The frozen comparison reads as follows:

```text
0.0 s  matched K1 and ball A setup
3.0 s  both 2.5-second model requests are pending
4.1 s  ball moves from A to B; both old requests continue
5.5 s  T2a accepts then host-blocks; T2b rejects before dispatch
7.0 s  fresh recovery requests capture current ball B
9.5 s  both current B results are accepted and dispatched once
10–20 s  both robots visibly walk towards the current B target
```

The paper-video acceptance gate is:

- the ball movement is visible without narration;
- both panels use the same crop, initial scene and intervention position;
- A, B, the obsolete target and both policy outcomes are readable at 1080p;
- neither robot starts towards the obsolete A target;
- both robots visibly translate towards the accepted current B target; and
- the hash-bound manifest reports zero obsolete-target calls and exactly one
  accepted current-B call per panel.

## gotchas

- T2a demonstrates the weaker runtime decision but remains physically safe
  because the independent Booster host gate still rejects the stale target.
- Do not describe T2a as issuing an unsafe walking command. The canonical
  evidence proves that it does not.
- The recovery is a second finite canonical run in the same continuous video
  capture. Its captured context must equal the moved-ball run's current
  context. Do not describe it as generation two of one BT instance.
- The panel calibration is valid only for the frozen camera and crop. Reframe
  the camera only after updating all five points and reviewing the result.
- A and B are editorial labels. The actual monotonic context IDs remain visible
  and are retained in the manifest.
- Disarm motion and restore the stock Booster agent after both captures.

## see also

- [T1 paper-video prototype](README.md)
- [Booster Studio host adapter](../README.md)
- [humanoid video experiment](../../README.md)
- [experiment contract](../../../../docs/project/humanoid-model-mediated-approach-contract.md)
