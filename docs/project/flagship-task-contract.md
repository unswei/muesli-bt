# flagship task and evidence contract

## what this is

This page fixes the task and evidence contract for the main `muesli-bt` `v1.0.0` flagship.

Use it when deciding whether new runtime, ROS2, capability, replay, or evaluation work strengthens the public release path.

This contract is intentionally narrower than the full roadmap. It exists to keep the release centred on one wheeled flagship rather than several competing scenarios.

## when to use it

Use this page when you:

- scope `v0.8.0` and `v0.9.0` work
- decide whether a capability, baseline, or evidence task belongs before `v1.0.0`
- define required artefacts for a flagship run
- review whether a proposed second scenario is helping or distracting

## how it works

### flagship task family

The main `v1.0.0` task family is dependable model-mediated wheeled robot behaviour on a physical platform.

The preferred public framing is:

- semantic inspection on a wheeled robot
- semantic navigation on a wheeled robot

The exact platform and environment may vary, but the release should centre on one reproducible wheeled flagship with one clear task contract.

The release posture is:

- preferred evidence: physical wheeled run
- acceptable fallback: Nav2-backed or rosbag-backed ROS2 run for the same task family if physical hardware readiness slips

### runtime claim supported by this contract

The flagship should support this product claim:

- BT semantics stay stable across transports and host deployments
- asynchronous model-backed outputs are untrusted until validated
- stale, invalid, late, or policy-violating outputs do not silently reach host execution
- canonical logs and replay artefacts make failures reproducible and explainable

### failure taxonomy

Every flagship condition should map failures into this shared taxonomy:

- `delay`: the capability or model response is slow but still within deadline
- `timeout`: the configured deadline expires before a usable result is accepted
- `stale_result`: a result arrives after it is still safe or relevant to use
- `invalid_payload`: the capability or model result fails schema or contract validation
- `unsafe_action`: the result passes shape checks but fails host-side safety or policy checks
- `backend_unavailable`: the configured backend cannot serve the request
- `cancel_ignored_until_completion`: cancellation is requested but the backend still completes late

The taxonomy is for release evidence and replay interpretation. It does not replace more detailed canonical events.

### evidence posture

Each flagship condition should produce the same minimum evidence bundle so runs can be replayed and compared consistently.

## api / syntax

### required artefacts for every flagship condition

Each flagship condition should produce:

- canonical `events.jsonl`
- run log
- manifest
- replay report
- rosbag, where applicable
- model request/response cache, where applicable
- video or time-aligned media note, where applicable

### minimum manifest content

The manifest for a flagship condition should identify at least:

- scenario name
- BT source or canonical DSL hash
- host capability configuration identity
- model backend identity or replay-cache mode
- fault schedule or seed, where applicable
- robot or simulator platform
- ROS2/Nav2 involvement, where applicable
- associated artefact paths

### acceptance posture

Before `v1.0.0`, new work should normally strengthen at least one of these:

- the flagship task itself
- the runtime contract and validation boundary
- the reproducible evidence bundle

If a change does not strengthen one of those three areas, it normally belongs after the release baseline is secure.

## example

A flagship condition might be:

- one wheeled robot is assigned a semantic inspection target
- a model-backed capability proposes a goal, waypoint, or short action suggestion
- `muesli-bt` submits, polls, validates, times out, cancels, accepts, or rejects that result
- the host executes only validated results
- the run emits canonical traces and supporting artefacts for replay and comparison

The same contract should still hold in the fallback ROS2/Nav2 or rosbag-backed path:

- same BT semantics
- same failure taxonomy
- same capability and validation story
- same required artefacts

## gotchas

- This contract is about one primary wheeled flagship, not several equal release stories.
- “Same BT, different IO transport” remains a foundation, not the full `v1.0.0` headline.
- Physical wheeled evidence is preferred, but the fallback path must stay honest and reproducible.
- The failure taxonomy should stay stable enough for replay reports and evaluation tables to use it directly.
- Manipulator or MoveIt work is supporting or stretch work until the wheeled flagship is already secure.
- The contract does not widen BT or Lisp semantics.

## see also

- [v1.0 direction](v1-direction.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
- [todo](../todo.md)
- [ROS2 backend scope](../integration/ros2-backend-scope.md)
