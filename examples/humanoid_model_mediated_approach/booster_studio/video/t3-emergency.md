# T3 higher-priority interruption paper video

## what this is

This workflow produces the polished 14-second T3 simulation shot. A Booster K1
submits a delayed model request. A controlled software emergency then activates
the higher-priority `safe_stand` branch while the request is still running.

The video shows the authority transition, not merely a stationary robot. A
live BT panel changes from `model_wait` to `safe_stand`. The request loses
authority immediately. When its late completion arrives, an animated result
packet is dropped and a red ghost trajectory shows the motion that was
prevented. The walking backend is never called.

The single ball is not labelled. The only pose marker is `GHOST TARGET · NEVER
DISPATCHED`.

## when to use it

Use this shot after the T1 normal-result and T2 moved-ball videos pass their
visual and evidence gates. T3 demonstrates that higher-priority Behaviour Tree
(BT) control pre-empts asynchronous model work without waiting for the model
service.

Retain the raw video, capture clock, live manifest, canonical `mbt.evt.v1`
stream and render manifest. The editorial overlay does not replace the event
stream.

## how it works

`t3_emergency.json` freezes:

- a 14-second shot with three seconds before request submission;
- the same one-K1, one-ball scene used for T1;
- a software emergency approximately one second into the fixed 2.5-second
  model delay; and
- fixed-camera calibration for the robot and revoked candidate target.

The controller asserts `/muesli/emergency` after the native runner emits the
request cue. On the next BT evaluation, the reactive selector enters
`safe_stand`, halts the model subtree and logically revokes its invocation.
The delayed service still finishes, but the runtime records the completion as
cancelled and drops it with `completion_after_cancel`.

`render_t3_emergency.py` places the live BT panel in the empty right-hand field
space without changing the simulator crop used for T1. The panel uses the same
amber, red and green authority language as the T1 and T2 overlays. The
renderer refuses to render unless the evidence proves all of these claims:

- T3 uses `invocation_scoped` acceptance;
- the software emergency occurs after submission;
- `safe_stand`, unstable state, revoked request state and cleared target state
  appear within one BT tick of the emergency;
- `async_authority_revoked` matches the job, generation and captured context
  and records `branch_revoked`;
- the later cancelled result contains a dropped candidate and is followed by
  `async_completion_dropped` with `completion_after_cancel`;
- there are no `walking_target_dispatch` events and the backend call count is
  zero; and
- the live manifest retains the full host safety envelope and is hash-bound to
  the event stream.

## api / syntax

Stage the common scene inside the virtual K1 container:

```bash
python3 video/stage_t1_scene.py --shot video/t3_emergency.json
```

Clear the controlled emergency, then arm motion after the bridge reports fresh
ball, robot-pose and stability data:

```bash
ros2 topic pub /muesli/emergency std_msgs/msg/Bool '{data: false}' \
  --rate 10 --times 5
ros2 topic pub /muesli/motion_arm std_msgs/msg/Bool '{data: true}' \
  --rate 10 --times 5
```

Start a clean capture, submit T3 and assert the emergency approximately one
second after `REQUEST_SUBMITTED`. The trial controller automates that ordering:

```bash
python3 /tmp/muesli-trial-controller.py \
  --trial T3 \
  --intervention emergency \
  --intervention-delay 1.0
```

Render the completed run on a host whose `ffmpeg` includes libass:

```bash
python3 video/render_t3_emergency.py \
  --events /tmp/t3-run/events.jsonl \
  --live-manifest /tmp/t3-run/live-manifest.json \
  --capture-timing /tmp/t3-clean/capture-timing.json \
  --raw-video /tmp/t3-clean/capture-full.mp4 \
  --output-dir /tmp/t3-polished
```

The output directory contains `t3-polished-emergency.mp4`, the generated ASS
overlay and `render-manifest.json` with hashes of every input and output.

## example

The frozen shot reads as follows:

```text
0.0 s  stable one-robot setup
3.0 s  delayed model request begins
4.1 s  software emergency activates safe_stand and revokes authority
5.5 s  late model completion is dropped; no target is dispatched
6.3 s  revoked candidate is shown only as evidence
14 s   safe stand remains active and the robot has not walked
```

The paper-video acceptance gate is:

- the emergency transition is unmistakable without narration;
- `safe_stand` and `branch_revoked` remain readable at 1080p;
- the red ghost target and trajectory are visually distinct from the green
  accepted-target language used in T1 and T2;
- the robot remains at its staged position; and
- the hash-bound manifest reports zero walking dispatch events and zero
  backend calls.

## gotchas

- The software emergency is a controlled experimental input. It does not
  replace the robot emergency stop or balance controller.
- A stationary robot alone is not persuasive evidence. Preserve the
  event-derived authority, branch, completion and dispatch overlays.
- The red candidate marker is forensic evidence from the cancelled response.
  It was never an authorised walking target.
- Keep the emergency asserted through the end of the take. Clear it only after
  disarming motion and finishing evidence collection.
- The pixel calibration is valid only for the frozen camera and crop.

## see also

- [T1 paper-video prototype](README.md)
- [T2 moved-ball paper-video comparison](t2-comparison.md)
- [Booster Studio host adapter](../README.md)
- [experiment contract](../../../../docs/project/humanoid-model-mediated-approach-contract.md)
