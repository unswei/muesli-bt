# humanoid model-mediated approach experiment contract

!!! note "status"

    Status: experimental software experiment harness plus core runtime safety
    primitives.

    This page defines the intended experiment and evidence contract. The
    runtime provides experimental invocation-scoped authority, pre-emption and
    reset cleanup, an explicit host commit-validation hook, and an
    SDK-independent three-component approach-pose validator for VLA BT nodes.
    It also provides matched baseline/full BTs, a delayed deterministic service,
    a four-trial runner, evidence manifests, an exactly-once walking-target host
    callback, a local Booster Studio host bridge and canonical dispatch
    evidence. The repository does not yet provide a Booster Studio or K1
    execution result, or the video overlay.

## what this is

This contract defines a humanoid experiment for asynchronous, model-mediated
Behaviour Tree (BT) decisions. A Booster K1 observes a ball and asks an external
approach proposer for a task-level target pose. The proposer responds after a
deliberate delay while the BT continues to tick.

The experiment tests whether a result is still authorised when it returns. A
result is useful only if the BT invocation that requested it is still active and
the observed ball context is still current.

The proposer may be either:

- a deterministic planner, used as the primary correctness backend; or
- a vision-language model (VLM), used when images form part of the request.

The experiment must not be described as a vision-language-action (VLA) system
unless the selected model has actually been trained to produce actions. The
proposer never controls joints, balance or footsteps. Existing walking and
safety controllers remain responsible for motion.

This is a supporting research experiment. It does not replace the wheeled
[flagship task and evidence contract](flagship-task-contract.md).

## when to use it

Use this contract when implementing, reviewing or recording the Booster K1
demonstration. It fixes:

- the authority rule for accepting an asynchronous result;
- the comparison between a deadline-only baseline and the full system;
- the task sequence and intervention timing;
- the required event evidence and video overlays; and
- the safety boundary between the BT, the proposer and the robot host.

Do not use this experiment as an evaluation of low-level locomotion quality or
model intelligence. The independent variables are the result-acceptance policy,
the ball context change and the higher-priority interruption.

## how it works

### system boundary

The experiment has four parts:

1. The Booster host observes the ball and robot safety state.
2. The BT submits an asynchronous request to the approach proposer.
3. The runtime decides whether the returned result is still authorised.
4. The Booster host validates an accepted pose before passing it to the existing
   walking controller.

The model or planner returns one task-level pose only. It must not return joint
targets, motor commands, balance commands or a timed footstep trajectory.

The approach pose is expressed in `ball_context`, a frame whose origin is the
observed ball position and whose axes are parallel to the configured field or
world frame. The request records the transform for that observation. This
avoids giving an orientation to the ball itself and makes the conversion to a
walking target unambiguous.

### invocation-scoped authority

Every submission captures an immutable authority record:

- `request_id`: unique within the run;
- `generation`: monotonically increasing for the logical request site;
- `request_node_id`: the BT node that owns the request;
- `ball_context_id`: the ball observation context at submission;
- `submitted_at_ns`: monotonic submission time; and
- `deadline_at_ns`: monotonic deadline.

Re-entering the request node creates a new generation. A result from an earlier
generation can never complete a later invocation, even when the implementation
reuses the same node or job key.

A result may be accepted exactly once, and only when all of these predicates are
true at the commit point:

```text
branch_is_authorised
and request_is_current_generation
and result_request_id_matches
and result_generation_matches
and result_ball_context_id_matches_current_context
and monotonic_now_is_not_after_deadline
and request_has_no_terminal_decision
and result_schema_is_valid
and host_pose_policy_accepts_result
```

If any predicate is false, the result is rejected and must not update the
walking target or dispatch a walking command.

Cancellation is best-effort resource management. Revocation is the logical
safety rule. A backend may finish after cancellation, but a revoked invocation
must still fail the commit check.

### ball context identity

The Booster perception adapter owns `ball_context_id`. It must change the ID
when any of the following occurs:

- the tracked ball is replaced;
- the track is lost and then reacquired; or
- ball displacement exceeds the configured context-change threshold.

The threshold, observation age limit and coordinate frame must be recorded in
the run manifest. Coordinates alone are not a context identifier. The same
coordinates may be observed at different times, and a moved ball may later
return to its previous coordinates.

For deterministic trials, use monotonically increasing context IDs within a
run, such as `ball-0001` and `ball-0002`.

### branch and interruption behaviour

The BT must have an explicit higher-priority safety branch. The intended control
flow is:

```text
reactive priority
├── emergency or unstable -> revoke model authority -> safe stand/stabilise
├── ball unavailable      -> revoke model authority -> search or safe wait
└── ball available
    ├── submit or poll the current request
    ├── remain in safe wait while it is running
    ├── commit an authorised result
    └── send the validated pose to the walking controller
```

When a higher-priority branch takes control, authority must be revoked within
the same BT tick. Physical cancellation may complete later. A subsequent result
must be rejected even if the backend reports successful completion.

### experiment modes

The same BT, proposer response, delay, robot configuration and ball observations
must be used in both modes. Only the commit policy changes.

| Mode | Commit policy | Intended use |
| --- | --- | --- |
| `deadline_only` | Checks completion, deadline, schema and host pose bounds, but does not compare the invocation generation or current ball context. | Research baseline only. It demonstrates why a timeout is insufficient. |
| `invocation_scoped` | Applies the complete authority predicate in this contract. | Full experiment system. |

The baseline is deliberately incomplete. Run it in simulation first. On the
physical robot, retain the independent host safety envelope and use a bounded
walking target or inhibit locomotion if required by the risk assessment.

### timing contract

Use a fixed artificial proposer delay of `2500 ms`. Values from `2000 ms` to
`3000 ms` are permitted, but a paper run must use one fixed value and record it
in the manifest.

Set the request deadline longer than the artificial delay. A default of
`3500 ms` is recommended. This ensures that the moved-ball trial tests stale
context rather than ordinary timeout behaviour.

The ball movement and emergency intervention must occur after the canonical
submission event and before the configured completion time. A software trigger
should record the intervention time. An audible or visible cue may guide a
human operator, but it is not the source of timing evidence.

### trial matrix

Each paper run must include all four rows below.

| Trial | Mode | Intervention | Required result |
| --- | --- | --- | --- |
| T1 normal | `invocation_scoped` | Leave the ball stationary. | Accept the current result once and dispatch its validated walking target. |
| T2a moved-ball baseline | `deadline_only` | Change the ball from context A to B while the request is running. | Demonstrate that the baseline has no context-based reason to reject the result for A. Under the fixed timing it is expected to accept that result, subject to the independent host safety envelope. |
| T2b moved-ball full | `invocation_scoped` | Repeat the same A-to-B change. | Reject the result for A with `context_changed`. Do not dispatch the obsolete target. Search, wait safely or submit a new request for B. |
| T3 interruption | `invocation_scoped` | Assert the software emergency or instability flag while the request is running. | Enter safe stand or stabilisation within one BT tick. Reject the late result with `branch_revoked`. Do not dispatch a walking command. |

Use the deterministic proposer for the required comparison. A VLM-backed
proposer may be shown as an additional trial, but it must use the same response
schema and commit gate. This keeps runtime correctness separate from model
quality and service variability.

### pass criteria

T1 passes when the current result receives exactly one `accepted` decision and
the matching target is dispatched exactly once.

T2b passes when all of the following are true:

- the result for context A is received after context B becomes current;
- the result is rejected with `context_changed`;
- the walking target for A is never dispatched after the context change; and
- a later request for B may be accepted normally.

T3 passes when all of the following are true:

- the safety branch becomes active no later than the first BT tick after the
  intervention is visible to the runtime;
- the outstanding authority is revoked;
- the late result is rejected with `branch_revoked`; and
- no walking target is dispatched after revocation.

The full experiment passes only when the canonical trace validates, replay
reproduces every acceptance decision, and the trace contains zero obsolete
walking-target dispatches in T2b and T3.

### observability and video overlay

The canonical `mbt.evt.v1` event stream is the source of truth. The video
overlay must be generated from, or reconciled against, that stream. Do not add a
second ad hoc JSONL log.

The overlay must display:

- active BT branch;
- request ID and generation;
- current ball context ID;
- request state;
- result decision and rejection reason; and
- current walking target.

Accepted current targets are green. Rejected or obsolete targets are red. No
target is shown when none has been authorised.

Use these stable values:

| Field | Values |
| --- | --- |
| Active branch | `safe_stand`, `stabilise`, `search`, `model_wait`, `model_execute`, `fallback` |
| Request state | `idle`, `queued`, `running`, `done`, `error`, `timeout`, `cancelled`, `revoked`, `rejected_safe_wait` |
| Result decision | `none`, `accepted`, `rejected` |
| Target state | `none`, `current`, `obsolete` |

Rejection reasons are stable machine-readable codes:

| Code | Meaning |
| --- | --- |
| `branch_revoked` | The owning BT branch no longer has authority. |
| `context_changed` | The current ball context differs from the captured context. |
| `superseded` | A newer generation exists for the logical request site. |
| `deadline_expired` | The result arrived after its monotonic deadline. |
| `cancelled` | The invocation was explicitly cancelled before commit. |
| `duplicate_terminal_result` | The invocation already has an accepted or rejected decision. |
| `invalid_schema` | The result does not match the response schema. |
| `invalid_frame` | The pose uses an unrecognised or disallowed frame. |
| `invalid_pose` | The pose contains non-finite or out-of-range values. |
| `ball_stale` | The current observation exceeds the configured age limit. |
| `robot_unstable` | The host stability policy rejects motion. |
| `duplicate_dispatch` | A walking target was already dispatched for this invocation. |
| `walking_controller_rejected` | The walking-controller boundary refused the target. |
| `host_policy_rejected` | Another documented host safety check rejects the pose. |

The implemented software experiment records these behaviours in `mbt.evt.v1`:

- request submission, polling and terminal backend status;
- captured request ID, generation, context ID and deadline;
- branch pre-emption and logical authority revocation;
- acceptance or rejection, including the current and captured context IDs;
- blackboard changes used by the overlay; and
- host walking-target validation and dispatch.

The generic commit gate, host-validation outcome, SDK-independent local pose
policy and canonical walking-target dispatch boundary are implemented and
observable. The local policy checks the action frame, configured pose bounds,
current ball context and robot stability. Dispatch rechecks the invocation,
context, frame, exact accepted target and exactly-once state before calling the
host. The Booster Studio scaffold now implements state acquisition,
observation-age and operating-area policy, a synchronous host-side dispatch
gate and a bounded pose follower. The C++ client polls that state and forwards
the invocation-scoped dispatch envelope. The complete path passes a local
SDK-independent smoke test. Booster package build and virtual-robot execution
remain experiment integration work.

The deterministic runtime suite covers normal acceptance, moved-ball context
change, supersession, late completion, duplicate completion, direct branch
halt, re-entry and emergency interruption. Each scenario has a separately
named CTest entry. The tests use explicit backend gates, fixed event timestamps
and no physical robot dependency.

The runnable harness is under
`examples/humanoid_model_mediated_approach/`. It includes matched
deadline-only and invocation-scoped BTs, the 2.5-second fake service, the four
frozen trial configurations and per-trial evidence protocols. The matrix runner
creates canonical event logs, trial summaries, validation reports and
provenance-rich run manifests. Each evidence protocol contains named predicates
that the runner evaluates against the event stream. Raw video, overlay video,
recorded-result replay and Booster simulation evidence remain explicit pending
artefacts.

Existing canonical event types should be extended where their meaning already
fits. Any new event type must be added to the v1 schema and documented in the
same implementation change.

### evidence artefacts

Store one self-contained bundle per run:

```text
examples/humanoid_model_mediated_approach/runs/<run_id>/
├── manifest.json
├── events.jsonl
├── event-validation.json
├── trial-summary.json
├── raw-video.mp4
├── overlay-video.mp4
└── replay-report.json
```

`events.jsonl` is the only event log. The other JSON files are summaries or
configuration artefacts, not competing logs.

The SDK-independent harness records a `null` observation-age limit and a
`not_simulated_by_sdk_independent_stub` policy. Such a run is not paper-eligible.
A Booster run must replace that policy with an enforced finite age limit.

The manifest must record:

- repository commit and dirty-worktree state;
- hardware or simulator identity;
- experiment mode and trial ID;
- proposer kind, model identity if applicable, and deterministic seed;
- artificial delay and request deadline;
- BT tick rate;
- coordinate frames and pose limits;
- ball movement and observation-age thresholds;
- ball contexts and intervention timestamps; and
- clock alignment used for the video overlay.

`trial-summary.json` must report at least the terminal decision, rejection
reason, number of walking-target dispatches, number of obsolete dispatches and
the measured request latency. The paper artefact may additionally report BT
tick latency and intervention-to-safe-branch latency.

### safety boundary

The Booster host retains final authority over all motion. It must reject a pose
that is non-finite, outside the allowed frame, outside configured distance or
yaw bounds, outside the permitted operating area, or unsafe for the current
stability state.

The core `approach_pose_validator` implements the finite-value, frame, local
bounds, ball-context and stability checks without a Booster SDK dependency. The
Booster adapter must supply the current context and stability snapshot, then
apply observation-age, operating-area and walking-controller policy.

Start with the software-controlled instability flag. Use a physical push only
after the software trial is reliable and after the relevant robot safety review.
The experiment does not weaken the robot emergency stop, balance controller,
collision limits or operator stop procedure.

## api / syntax

The following JSON shapes are the contract between the Booster host and the
approach proposer. They are not yet a released muesli-bt public API.

### request

```json
{
  "schema_version": "humanoid.approach_request.v1",
  "request_id": "req-0042",
  "generation": 7,
  "request_node_id": "choose_ball_approach",
  "ball_context_id": "ball-0001",
  "submitted_at_ns": 184200000000,
  "deadline_at_ns": 187700000000,
  "observation": {
    "image_uri": "cache://run-17/frame-0042.jpg",
    "ball_position_m": [1.20, -0.35, 0.0],
    "ball_position_frame": "field",
    "field_T_ball_context": [1.20, -0.35, 0.0]
  },
  "constraints": {
    "output_frame": "ball_context",
    "max_offset_m": 1.0,
    "allowed_yaw_rad": [-3.141593, 3.141593]
  }
}
```

`image_uri` is optional for a deterministic geometric planner. It is required
when the proposer is described as a VLM.

### response

```json
{
  "schema_version": "humanoid.approach_response.v1",
  "request_id": "req-0042",
  "generation": 7,
  "ball_context_id": "ball-0001",
  "approach_pose": {
    "frame_id": "ball_context",
    "x_m": -0.45,
    "y_m": 0.08,
    "yaw_rad": 0.0
  },
  "confidence": 0.91
}
```

`confidence` is diagnostic only. It must not override the authority or host
safety checks.

### implementation configuration

The experiment configuration must make the comparison explicit:

```json
{
  "experiment": "humanoid-model-mediated-approach",
  "trial": "T2b",
  "acceptance_policy": "invocation_scoped",
  "proposer": "deterministic",
  "artificial_delay_ms": 2500,
  "deadline_ms": 3500,
  "ball_context_change_threshold_m": 0.15,
  "ball_observation_max_age_ms": 500,
  "physical_motion_enabled": true
}
```

Threshold values above are examples. Freeze the chosen values before collecting
paper results and record them in every run manifest.

## example

The moved-ball full-system trial has the following expected timeline:

```text
t = 0.00 s  the ball becomes current as ball-0001
t = 0.10 s  req-0042 generation 7 is submitted for ball-0001
t = 1.00 s  operator moves the ball beyond the context threshold
t = 1.05 s  perception publishes ball-0002
t = 2.60 s  response for req-0042 and ball-0001 becomes available
t = 2.61 s  commit gate compares ball-0001 with current ball-0002
t = 2.61 s  result is rejected: context_changed
t = 2.61 s  no walking target for ball-0001 is dispatched
t = 2.70 s  BT searches, waits safely or submits a request for ball-0002
```

The deadline-only baseline uses the same sequence. It lacks the context
comparison, so the response for `ball-0001` remains eligible before its
deadline. That difference is the experimental contrast.

## gotchas

- A timeout bounds age. It does not prove that the world context is unchanged.
- Backend cancellation does not revoke logical authority by itself.
- A node ID is not an invocation ID. Re-entry must increment the generation.
- Result parsing must not write a candidate pose directly into the live walking
  target. Commit to the walking target only after every check passes.
- Context identity must be based on perception lifecycle and movement policy,
  not exact floating-point coordinate equality.
- Model confidence is not a safety signal.
- Replay must use recorded results and decisions. It must not call the live
  proposer.
- The deadline-only policy is an experimental baseline, not a production mode.
- Isaac H1 integration does not imply Booster K1 support. The Booster adapter,
  walking target interface and stability flag require their own integration.
- The core approach-pose validator and walking-target dispatcher are not
  Booster SDK adapters. The registered host callback owns the real controller
  hand-off.

## see also

- [flagship task and evidence contract](flagship-task-contract.md)
- [v1.0 direction](v1-direction.md)
- [VLA request/response contract](../bt/vla-request-response.md)
- [approach pose host validation](../bt/approach-pose-validation.md)
- [VLA logging](../observability/vla-logging.md)
- [canonical event log](../observability/event-log.md)
- [terminology](../terminology.md)
