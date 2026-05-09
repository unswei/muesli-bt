# VLA backend integration plan

!!! note "status"
    Status: planned.
    This plan targets `v0.8.0` and is hardened through `v0.9.0` and `v1.0.0`. It is not released backend functionality yet.

## purpose

This document defines the first practical model integrations for the `muesli-bt` VLA/model-backed asynchronous path.

The goal is not to make `muesli-bt` a model-serving framework. The goal is to prove that `muesli-bt` can supervise heavy, delayed, fallible AI model calls as transport-transparent asynchronous host capabilities.

The selected integrations should convince potential users that:

- the Behaviour Tree (BT) source does not care where the model runs
- model calls are cancellable and deadline-aware
- stale or invalid outputs do not silently reach the robot
- request/response records are replayable
- the same runtime contract can support small practical Vision-Language-Action (VLA) models and larger research VLA models

## selected model backends

The first two integrations should be:

1. SmolVLA through LeRobot async inference
2. MiniVLA through the OpenVLA-Mini adapter path
3. OpenVLA-OFT through a model-serving adapter

Both are VLAs. This is deliberate. For user adoption, VLAs are a stronger first integration target than a world model because they naturally stress the system features `muesli-bt` is designed to provide:

- asynchronous execution
- network or edge-server deployment
- long and variable inference latency
- stale-result rejection
- validation before execution
- fallback
- replayable decision records

A world model integration can come later. It is less urgent for proving the host-capability and async orchestration story to potential users.

## architectural rule

From the BT/runtime point of view, a VLA is just an asynchronous host capability.

The BT source should depend on:

- capability name
- request schema
- result schema
- deadline
- validation policy
- fallback policy

The BT source should not depend on:

- whether the model runs onboard
- whether the model runs on an edge GPU server
- whether the model is called through HTTP, gRPC, ROS2, local process, or replay cache
- GPU placement
- authentication details
- batching implementation
- model-specific Python libraries

Model placement is deployment configuration, not BT semantics.

## intended split

```text
muesli-bt runtime
  owns:
    submit / poll / cancel / timeout
    validation
    fallback
    stale-result rejection
    canonical logging
    replay records
    BT semantics

VLA backend adapter
  owns:
    model-specific input formatting
    transport
    authentication, if needed
    model process or service lifecycle
    response decoding
    backend-specific error mapping
```

## capability names

Prefer generic capability names in BT source. Model-specific names belong in backend config and logs.

Recommended initial capabilities:

```text
cap.vla.action_chunk.v1
cap.vla.propose_nav_goal.v1
cap.vla.select_target.v1
cap.vla.propose_manipulation_action.v1
```

Use only the capabilities needed for the first demos. Do not expose a large speculative VLA surface.

For the first integration, prefer:

```text
cap.vla.action_chunk.v1
```

This is the most direct fit for LeRobot-style async inference.

If the first demo is navigation rather than manipulation, use:

```text
cap.vla.propose_nav_goal.v1
```

The implementation should not hard-code `smolvla` or `openvla` into the BT source.

## backend 1: SmolVLA / LeRobot async

SmolVLA should be one practical integration behind `cap.vla.action_chunk.v1`.

It should show that an accessible robotics-model stack can be connected to `muesli-bt` without changing BT semantics.

SmolVLA through LeRobot async inference is a good first backend because:

- it is lightweight relative to larger VLA models
- it is part of a developer-facing robotics learning ecosystem
- it already has a natural policy-server / robot-client style deployment
- it maps cleanly to submit, poll, cancel, timeout, and replay
- it is more likely to be reproduced by early users than a heavyweight custom model stack

Support at least one real mode and one replay mode:

```text
local_process
http_edge_server
replay_cache
```

`local_process` may wrap a local LeRobot policy server.

`http_edge_server` may call a nearby workstation or edge GPU server.

`replay_cache` replays recorded request/response pairs for deterministic tests.

### minimum request schema

Example for `cap.vla.action_chunk.v1`:

```json
{
  "schema": "cap.vla.action_chunk.request.v1",
  "instruction": "pick up the red block",
  "scene": {
    "timestamp_ns": 123456789,
    "frame_id": "camera_link",
    "image_handle": "media://run-001/rgb/000123.png",
    "robot_state": {
      "joint_positions": [],
      "gripper_state": "open"
    }
  },
  "horizon": 8,
  "deadline_ms": 500
}
```

The schema should allow image handles rather than forcing large image blobs into every event record.

### minimum result schema

```json
{
  "schema": "cap.vla.action_chunk.result.v1",
  "status": "ok",
  "scene_timestamp_ns": 123456789,
  "actions": [
    {"type": "joint_delta", "values": [0.01, 0.0, -0.02]},
    {"type": "joint_delta", "values": [0.01, 0.0, -0.01]}
  ],
  "confidence": 0.73,
  "model_info": {
    "family": "smolvla",
    "backend": "http_edge_server"
  }
}
```

For the first implementation, it is acceptable to use a simplified action representation if the demo does not execute raw low-level actions. The preferred safe path is to have the VLA propose short-horizon action candidates that are then validated or converted by a host-side controller.

## backend 2: MiniVLA / OpenVLA-Mini

MiniVLA should be a peer practical integration to SmolVLA, not a new BT feature and not a new public capability id.

MiniVLA through `muesli-model-service` is useful because:

- it exercises the same `cap.vla.action_chunk.v1` contract with a different VLA family
- it keeps model selection in service configuration rather than BT source
- it can use the same HTTP frame ingest and `frame://` reference flow as SmolVLA
- it provides a smaller OpenVLA-family alternative before heavier OpenVLA-OFT work

The service should select it with `MMS_ACTION_CHUNK_BACKEND=minivla` and report `backend: "minivla"` plus `adapter: "openvla-mini"` in metadata. `muesli-bt` must not branch on those fields for BT semantics.

MiniVLA uses the same request and result shapes as SmolVLA:

- `instruction`
- `observation.state`
- `observation.images`
- `frame://...` image references resolved by the service frame store
- `status: "action_chunk"` with `output.actions[]`

The release evidence may use either SmolVLA or MiniVLA as the first practical backend if the selected path has real inference, replayable requests, validation outcomes, and documented limitations.

## shared validation requirements

Before any result can affect execution:

- scene timestamp must be fresh enough
- action vector dimensions must match the declared action space
- action magnitudes must be within bounds
- command delta must be within configured limits
- confidence handling must follow policy
- robot mode must permit action use
- fallback must be available if validation fails

## backend 3: OpenVLA-OFT

OpenVLA-OFT should be the heavyweight credibility integration.

It should show that `muesli-bt` can supervise a larger recognised VLA stack as a remote or edge-hosted service.

OpenVLA-OFT is useful because:

- it represents a more serious large-model VLA path
- it is recognisable to robotics researchers
- it makes model latency and deployment placement a real issue
- it stresses cancellation, stale-result rejection, and replay more strongly than a small local toy backend

Support at least:

```text
http_edge_server
replay_cache
```

A local-process mode is optional if the dependency footprint is manageable.

Use one of:

```text
cap.vla.action_chunk.v1
cap.vla.propose_manipulation_action.v1
cap.vla.select_target.v1
```

The first OpenVLA-OFT integration should avoid requiring a full manipulation hardware stack unless one is already available and stable. It is acceptable for the backend to propose target/action candidates that are validated and either executed in simulation or rejected.

The adapter must map model output into a generic result schema. Model-specific raw output should be stored only as backend metadata or as a redacted/debug artefact.

Required result fields:

- `status`
- `scene_timestamp_ns`
- proposed action, target, or action chunk
- model family/version metadata
- backend type
- confidence or score, if available
- validation status
- request hash and response hash

## configuration model

The same BT should be runnable with different VLA placements by changing config only.

Example configuration for SmolVLA over HTTP:

```yaml
capabilities:
  cap.vla.action_chunk.v1:
    backend: http
    model_family: smolvla
    endpoint: http://edge-gpu.local:8080/v1/action_chunk
    timeout_ms: 500
    replay_cache: runs/vla-cache/smolvla/
    request_schema: schemas/cap.vla.action_chunk.request.v1.json
    result_schema: schemas/cap.vla.action_chunk.result.v1.json
    validation_policy: configs/validation/action_chunk_safe.yaml
    redaction_policy: configs/redaction/vla_default.yaml
```

Example configuration for OpenVLA-OFT over HTTP:

```yaml
capabilities:
  cap.vla.action_chunk.v1:
    backend: http
    model_family: openvla-oft
    endpoint: http://edge-gpu.local:8090/v1/action_chunk
    timeout_ms: 1000
    replay_cache: runs/vla-cache/openvla-oft/
    request_schema: schemas/cap.vla.action_chunk.request.v1.json
    result_schema: schemas/cap.vla.action_chunk.result.v1.json
    validation_policy: configs/validation/action_chunk_safe.yaml
    redaction_policy: configs/redaction/vla_default.yaml
```

Example replay configuration:

```yaml
capabilities:
  cap.vla.action_chunk.v1:
    backend: replay_cache
    model_family: smolvla
    replay_cache: fixtures/vla/smolvla/action_chunk/
    timeout_ms: 500
    validation_policy: configs/validation/action_chunk_safe.yaml
```

## BT usage

The BT source should remain generic.

Example sketch:

```lisp
(defbt vla-assisted-step
  (sel
    (seq
      (cond scene-fresh)
      (vla-request
        :capability "cap.vla.action_chunk.v1"
        :input scene-handle
        :instruction current-instruction
        :deadline-ms 500
        :validate "action_chunk_safe"
        :fallback stop)
      (act execute-validated-action-chunk))
    (act stop)))
```

The BT should not mention:

```text
SmolVLA
OpenVLA-OFT
HTTP endpoint
GPU id
Python module
cloud provider
```

Those are backend configuration details.

## runtime lifecycle

Every VLA request should follow the same lifecycle:

```text
submit
poll
partial, optional
complete or timeout
validate
commit or reject
fallback, if needed
log
replay record
```

Cancellation path:

```text
submit
poll
cancel requested
cancel acknowledged or cancel late
late result arrives, if any
late result dropped
fallback or continue
log
```

The lifecycle must be the same across SmolVLA, OpenVLA-OFT, local process, HTTP, ROS2 action, and replay cache backends.

## canonical events

Use existing event names where available. If new events are needed, add canonical equivalents for:

```text
vla_submit
vla_partial
vla_result
vla_timeout
vla_cancel_requested
vla_cancel_acknowledged
vla_cancel_late
model_result_dropped
action_validation
fallback_used
capability_backend_error
capability_replay_record
```

Each event should include:

- run id
- tick id
- node id
- capability name
- backend type
- model family
- job id
- request hash
- response hash, if available
- scene timestamp
- deadline
- latency
- validation status
- fallback status
- cancellation status
- replay record id

Do not log raw images or sensitive prompts directly into `events.jsonl`. Use media handles and redacted summaries.

## replay records

Each model call should have a replayable record.

Suggested record:

```json
{
  "schema": "mbt.vla.replay_record.v1",
  "capability": "cap.vla.action_chunk.v1",
  "model_family": "smolvla",
  "backend_type": "http",
  "request_hash": "sha256:...",
  "response_hash": "sha256:...",
  "request_summary": {
    "instruction_hash": "sha256:...",
    "scene_timestamp_ns": 123456789,
    "image_handle": "media://run-001/rgb/000123.png"
  },
  "response_summary": {
    "status": "ok",
    "action_count": 8,
    "confidence": 0.73
  },
  "deadline_ms": 500,
  "latency_ms": 217.4,
  "validation": {
    "valid": true,
    "policy": "action_chunk_safe"
  },
  "redaction": {
    "raw_prompt_stored": false,
    "raw_image_stored": false
  }
}
```

Replay mode should be able to run without the model backend being available.

## validation policy

Validation is mandatory. A model result is untrusted until validated.

Minimum validation rules for action chunks:

- schema matches expected result type
- scene timestamp is fresh
- action count does not exceed configured horizon
- action dimension matches robot/action-space declaration
- numeric values are finite
- numeric values are within configured bounds
- per-step delta is within configured limits
- command age is within limit
- fallback action exists
- action is not emitted if validation fails

Minimum validation rules for target proposals:

- target frame is known
- target timestamp is fresh
- target pose is finite
- target lies within configured workspace or navigation bounds
- optional Nav2 or MoveIt feasibility checks pass, where available
- fallback action exists

Validation result object:

```json
{
  "valid": false,
  "reason": "stale_scene_timestamp",
  "field": "/scene_timestamp_ns",
  "source": "cap.vla.action_chunk.v1",
  "fallback": "stop",
  "reached_host": false
}
```

## failure injection

The integration must support deterministic injected failures for tests and evaluation figures.

Required failure modes:

```text
delayed_success
timeout
backend_unavailable
invalid_schema
unsafe_action_value
stale_scene_timestamp
dropped_response
cancel_ignored_until_complete
late_result_after_cancel
partial_then_invalid_final
```

The same seed should reproduce the same failure schedule.

Example config:

```yaml
fault_injection:
  seed: 42
  latency_model: lognormal
  latency_ms_mean: 250
  latency_ms_sigma: 100
  timeout_probability: 0.05
  invalid_schema_probability: 0.05
  stale_scene_probability: 0.10
  unsafe_action_probability: 0.05
```

## benchmark and evidence requirements

The VLA integration is not complete until it produces evidence, not only successful demos.

Required plots or tables:

1. Tail latency under model lag:
   - p50, p95, p99, p999 latency
   - nominal versus injected heavy-tail latency
   - SmolVLA and OpenVLA-OFT reported separately
2. Cancellation and stale-result outcomes:
   - submitted jobs
   - cancelled jobs
   - cancellation acknowledged
   - cancellation late
   - late results dropped
   - stale results rejected
   - results committed
3. Validation and fallback table:
   - invalid outputs produced
   - invalid outputs rejected
   - invalid outputs reaching host, expected to be zero
   - fallback activations
   - fallback failures
4. Replay evidence:
   - run with live backend
   - run from replay cache
   - first-divergence result if replay record is deliberately altered
5. Transport transparency evidence:
   - same BT source against local/replay or HTTP/replay backend
   - ideally same BT source against SmolVLA and OpenVLA-OFT configs where schemas match

## implementation tasks

### v0.8.0 minimum tasks

```text
[ ] define generic VLA capability schema(s)
[ ] implement capability backend config loader
[ ] implement replay_cache backend
[ ] implement SmolVLA backend adapter
[ ] implement OpenVLA-OFT backend adapter or stubbed HTTP adapter against a documented serving contract
[ ] implement request/response hashing
[ ] implement redaction policy for prompts/media
[ ] implement validation policy for VLA results
[ ] implement canonical VLA lifecycle events
[ ] implement deterministic latency/failure injection
[ ] add VLA replay fixtures
[ ] add one runnable SmolVLA demo or replay-backed equivalent
[ ] add one OpenVLA-OFT demo or replay-backed equivalent
[ ] document backend configuration and deployment modes
```

### v0.9.0 hardening tasks

```text
[ ] run matched VLA latency/failure benchmarks
[ ] connect at least one VLA capability to the comparison harness
[ ] include VLA lifecycle events in neutral comparison traces where relevant
[ ] test VLA behaviour under ROS callback pressure if used in ROS-backed demo
[ ] include VLA trace bundle in evaluation evidence outputs
[ ] add model backend failure classification to experiment manifests
```

### v1.0.0 release tasks

```text
[ ] freeze VLA capability schema version(s)
[ ] freeze replay record schema version
[ ] publish trace bundles for live and replay VLA runs
[ ] publish scripts to regenerate VLA figures/tables
[ ] document exact model/backend versions used for evidence artefacts
[ ] mark any non-reproducible model path as supporting evidence only
[ ] clearly label production limitations
```

## acceptance criteria

### v0.8.0

- At least one real VLA backend is callable through the host capability path.
- The stub backend is no longer the only release evidence path.
- The same BT source can run against a live backend and a replay backend.
- Model outputs are validated before execution.
- Stale or invalid outputs are rejected deterministically.
- Canonical logs include the model-call lifecycle.
- Replay records allow the model-call path to be reproduced without the model running.

### v0.9.0

- VLA latency, cancellation, stale-result, validation, and fallback behaviour are measured under seeded fault schedules.
- At least one VLA-backed scenario participates in the comparison/evidence harness.
- The VLA backend path remains a host capability and does not leak model-specific semantics into the BT core.

### v1.0.0

- The release includes at least one real model-backed VLA or model-mediated async experiment.
- The release includes replayable request/response records.
- The release includes clear documentation of model placement transparency.
- The release includes honest limitations for production VLA control.
- The release evidence supports transport-transparent async VLA integration under the runtime contract.

## non-goals

The first VLA integrations should not attempt to:

- train new VLA models
- claim state-of-the-art VLA performance
- embed large model inference inside the BT runtime
- make raw model text executable
- bypass host-side safety validation
- replace Nav2, MoveIt, robot drivers, or low-level controllers
- make cloud inference appear equivalent to local inference without reporting latency and failure modes

## see also

- [VLA integration](../bt/vla-integration.md)
- [VLA request/response](../bt/vla-request-response.md)
- [host capability bundles](host-capability-bundles.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
