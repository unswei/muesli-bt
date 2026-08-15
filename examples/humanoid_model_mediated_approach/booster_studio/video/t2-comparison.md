# T2 moved-ball paper-video comparison

## what this is

This workflow produces the polished 20-second T2 simulation comparison. The
left panel is the deliberately unprotected timeout-only T2a baseline. The right
panel is the full invocation-scoped T2b system. Both panels begin with the same
Booster K1, ball pose and camera view.

The ball moves conspicuously while the same deterministic 2.5-second model
request is running. The simulation-only baseline accepts the obsolete result
and walks towards target A, which was calculated from the original ball
position. The full system rejects the obsolete result, requests a fresh pose
and walks towards target B, which was calculated from the current ball
position.

The video does not name the single ball. It labels only the two distinct
approach targets: `TARGET A · OBSOLETE` and `TARGET B · CURRENT`.

## when to use it

Use this comparison after the T1 visual-quality gate passes. Use matched fresh
captures rather than duplicating one clip because the video must show that both
policies ran in the simulator.

Retain both raw videos, the three canonical `mbt.evt.v1` streams, both capture
clocks, the T2a live manifest and the render manifest. The left capture contains
one moved-ball T2a trial. The right continuous capture contains the moved-ball
T2b trial followed by one normal recovery trial. The editorial overlay does
not replace those evidence artefacts.

Never use the unsafe baseline profile on hardware. A physical-robot video must
run the full system with the host context gate enabled.

## how it works

`t2_comparison.json` freezes:

- a 20-second comparison with three seconds before each moved-ball request;
- equal 960-pixel panels in a 1080p output;
- identical K1 and original ball poses;
- the current ball position at `(1.1, 0.65)` metres in the field frame; and
- fixed-camera calibration for target A and target B.

The trial controller moves the ball one second after `REQUEST_SUBMITTED`. On
the left, `MUESLI_BOOSTER_UNSAFE_SIM_BASELINE=true` permits only T2a to resolve
the stale ball-relative pose against its retained original context anchor. The
runner passes an explicit `require_context_match=false` option through the
otherwise fail-closed C++ structural dispatch gate. The baseline therefore
dispatches target A. The override defaults to false, is cleared when the trial
ends and is revoked on explicit ball loss.

On the right, the ordinary host envelope remains enabled. T2b rejects the stale
result before dispatch. The controller then submits a normal-result trial
without resetting the scene. That request captures the current context and
dispatches target B.

`render_t2_comparison.py` aligns each clean capture to its first canonical
`vla_submit` event. It refuses to render unless the evidence proves all of
these claims:

- T2a uses `deadline_only` and accepts the stale result;
- T2a records one accepted `walking_target_dispatch` with
  `context_match_required=false` and one backend call;
- the T2a live manifest declares `unsafe_simulation_baseline`;
- T2b uses `invocation_scoped`, rejects with reason `context_changed` and
  records no stale walking-target dispatch;
- both moved-ball trials correlate generation, job and captured context;
- both trials record the same original ball position, current ball position
  and ball-relative candidate;
- the recovery captures the current context; and
- the recovery accepts one current result and records exactly one backend call.

Target A is derived from the original ball position. Target B is derived from
the current ball position. Red and green screen points use the explicit,
hash-bound fixed-camera calibration.

## api / syntax

Stage the common scene inside the virtual K1 container. The T1 staging helper
accepts the T2 comparison configuration because both use the same paper-video
scene fields:

```bash
python3 video/stage_t1_scene.py --shot video/t2_comparison.json
```

Start the Agent with the unsafe simulation override only for the baseline
capture:

```text
MUESLI_BOOSTER_UNSAFE_SIM_BASELINE=true
```

Arm motion only after the bridge reports fresh, stable state. Start a clean
24-second capture about four seconds before T2a, then move the ball one second
after the request cue. Do not submit a recovery on the baseline side.

Restart the Agent without the unsafe environment variable, reset the scene and
record the full side. Run T2b, move the ball at the same delay, then keep the
current ball position and submit T1 for the fresh recovery.

Render the matched captures on a host whose `ffmpeg` includes libass:

```bash
python3 video/render_t2_comparison.py \
  --baseline-events /tmp/t2a-run/events.jsonl \
  --baseline-live-manifest /tmp/t2a-run/live-manifest.json \
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
0.0 s   matched K1 and ball setup
3.0 s   both 2.5-second model requests are pending
4.1 s   the ball moves; both old requests continue
5.5 s   baseline dispatches target A; full system rejects target A
7.0 s   full system requests a pose for the current ball position
9.5 s   full system accepts and dispatches target B
10–20 s robots visibly diverge towards target A and target B
```

The paper-video acceptance gate is:

- the ball movement is visible without narration;
- both panels use the same crop, initial scene and intervention position;
- target A, target B and both policy outcomes are readable at 1080p;
- the baseline visibly translates towards obsolete target A;
- the full system never starts towards target A and visibly translates towards
  current target B; and
- the hash-bound manifest reports one obsolete-target call on the baseline and
  one current-target call on the full system.

## gotchas

- T2a is intentionally unsafe and simulation-only. Its purpose is to expose the
  failure hidden by the ordinary independent host backstop.
- The default remains fail-closed. Enabling motion does not enable the unsafe
  profile.
- The full-side recovery is a second finite canonical run in the same
  continuous video capture. Its captured context must equal the moved-ball
  run's current context.
- The panel calibration is valid only for the frozen camera and crop. Reframe
  the camera only after updating both target points and reviewing the result.
- Target A and target B are editorial labels for distinct approach poses. The
  ball is not labelled. The monotonic context IDs remain visible.
- Disarm motion, remove the unsafe environment variable and restore the stock
  Booster agent after capture.

## see also

- [T1 paper-video prototype](README.md)
- [Booster Studio host adapter](../README.md)
- [humanoid video experiment](../../README.md)
- [experiment contract](../../../../docs/project/humanoid-model-mediated-approach-contract.md)
