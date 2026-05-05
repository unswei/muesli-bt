# roadmap to 1.0

## what this is

This page turns the current backlog, ROS2 scope, conformance work, and evidence-oriented release planning into one release roadmap from the current baseline to `v1.0.0`.

This page is release-oriented. The shorter [Roadmap](limitations-roadmap.md) page remains the theme-based backlog.

## when to use it

Use this page when you:

- decide whether a change belongs before or after `v1.0.0`
- scope release work for `v0.5.0+`
- review whether ROS2 work is staying thin and bounded
- check what evidence is still missing for a release-ready baseline

## how it works

### guiding constraints

The release path to `v1.0.0` should preserve three constraints.

1. ROS2 stays a thin [host](terminology.md#host) integration layer. It proves deployability. It does not redefine BT, planner, or Lisp semantics.
2. ROS2 is also a conformance surface. Rosbag replay should strengthen the deterministic replay and observability story.
3. One canonical external event stream remains the contract surface: `mbt.evt.v1`.

Higher-level ROS libraries should not be forced into `env.*` or `planner.plan`.
They should land as host capability bundles with their own contracts at the host boundary.

In this roadmap, a host capability bundle means:

- one named host-side capability family
- one stable contract surface exposed to BT logic
- one or more concrete adapters behind that contract

Examples:

- transport backend bundle: direct control-loop IO through `env.*`
- bounded decision planner bundle: `planner.plan`
- manipulator motion bundle: a future MoveIt-backed arm-motion surface
- perception bundle: a future state-estimation or scene-understanding surface

In practical terms, ROS2 is important for `v1.0.0` because it should prove:

1. a BT loop can run as a ROS2 node with clean start, stop, and shutdown behaviour
2. rosbag-driven runs can reproduce deterministic logs and stable decision traces for fixed inputs
3. the optional ROS2 integration does not contaminate core runtime semantics or non-ROS builds

### current baseline

The project is not starting this roadmap from zero.

Already shipped in `v0.2.0`:

- runtime contract `v1`
- canonical event schema `mbt.evt.v1`
- deterministic fixtures and validation tooling
- `L0` and `L1` conformance lanes
- release packaging for source, Ubuntu `x86_64`, and macOS `arm64`

Already shipped across `v0.3.0` and `v0.3.1`:

- first Linux ROS2 transport path on Ubuntu 22.04 + Humble
- real `Odometry` input and `Twist` output transport binding
- installed-package ROS2 consumer smoke coverage
- first rosbag-backed `L2` replay corpus and artefact verification
- ordinary CI gating for the rosbag-backed `L2` lane
- a ROS-enabled Ubuntu 22.04 + Humble release artefact path alongside the generic Ubuntu archive

Already shipped in `v0.4.0`:

- direct canonical `mbt.evt.v1` output for ROS-backed runs
- trace-level replay validation alongside per-record schema validation
- explicit canonical lifecycle anchors for longer runs (`episode_begin`, `episode_end`, `run_end`)
- explicit ROS timing metadata in canonical logs (`time_source`, `use_sim_time`, `obs_timestamp_source`)
- one shared `events.jsonl` artefact shape for simulator-backed and ROS-backed runs

The repository also now includes a checked-in benchmark harness and a documented Isaac Sim / ROS2 H1 showcase path.
Those help evidence gathering, but they do not replace the remaining release gates for `v1.0.0`.

The current release path also carries the `v0.5.0` cross-transport flagship baseline:

- one shared wheeled BT reused across Webots, PyBullet, and ROS2
- a supported ROS2 tutorial from build through canonical log validation
- scripted cross-transport comparison checks
- a stricter same-robot comparison lane through the PyBullet e-puck-style surrogate
- supporting Isaac Sim / ROS2 wheeled showcase media and docs

This means the remaining path to `v1.0.0` is not “add ROS2 somehow” or “make replay work somehow”.
The thin adaptor and observability baseline are already real enough that the next question is how to exploit them without widening the semantic surface.

The remaining path is:

- preserve “same BT, different IO transport” as the first release evidence point
- harden the Lisp/C++ runtime so the evidence can defend allocation behaviour, GC pauses, deadline handling, cancellation, and deterministic replay
- replace VLA stubs with at least one real model-backed asynchronous service and failure-injection path
- build a fair comparison engine against BehaviorTree.CPP rather than relying only on internal microbenchmarks
- add ROS2 host capability bridges, especially Nav2 and, if still feasible, MoveIt, without widening core runtime semantics
- keep the existing Isaac showcase as supporting evidence rather than a second semantic surface
- make the Lisp-as-DSL argument explicit and testable: Lisp is used as a compact structured representation for BTs, not as arbitrary scripting; before `v1.0.0`, include at least one implemented demonstration where a BT fragment is generated as Lisp data, validated, compiled, serialised, safely installed at a tick boundary, logged, and replayed
- keep expected engine features inside `muesli-bt` itself: host capability authoring, blackboard/schema validation, diagnostics, CLI validation, metadata export, embedding, and replayable artefacts belong in the runtime rather than requiring a visual tool
- finish evaluation, trace bundles, and release hygiene

### engine and studio boundary

The remaining roadmap should keep the boundary between the engine and visual tooling explicit.

`muesli-bt` should provide:

- runtime execution and engine APIs
- the Lisp BT authoring surface
- validation, diagnostics, and CLI workflows
- canonical event emission and replayable artefacts
- blackboard/schema and capability integration points
- tree metadata exports for external tools
- embedding and host integration examples

`muesli-studio` should provide:

- visual inspection
- tick scrubbing
- timelines
- replay UI
- live monitoring
- later visual authoring

The practical engine-side gaps to close before `v1.0.0` are:

- a clean host-capability and custom-node authoring workflow
- a clear dataflow and blackboard schema story
- useful error diagnostics for BT, Lisp, blackboard, capability, and validation failures
- safety, fallback, and watchdog hooks at the task-runtime boundary
- stable embedding and CLI workflows for larger robot stacks
- canonical engine metadata for tools such as `muesli-studio`, without implementing the UI in `muesli-bt`

### Lisp DSL argument

The release documentation should explain Lisp as a technical choice, not as a matter of taste.

The argument is:

- Behaviour Trees are trees.
- Lisp S-expressions are executable structured tree data.
- A Lisp BT DSL can therefore serve as human-authored source, generated intermediate representation, validation input, serialisation format, replay artefact, and runtime patch format.
- This is useful for robotics because task logic can be generated or transformed without becoming opaque host code.
- The runtime can validate the generated tree against BT grammar, host capability contracts, budget rules, fallback requirements, and safety restrictions before it is allowed to execute.
- The same canonical event stream can record which generated tree was used, when it was validated, when it was installed, and how it behaved during replay.

The release claim should be narrow:

> `muesli-bt` uses Lisp as an inspectable, executable tree representation for robot task logic. This lets the same representation support authored BTs, generated BT fragments, validation, serialisation, replay, and safe tick-boundary installation under the runtime contract.

The claim should not be:

- Lisp is generally better than C++, XML, YAML, or Python.
- Lisp makes the system hard real-time.
- Lisp is required for all BT systems.
- Generated BTs are automatically safe without validation.

The release must include one concrete demonstration that makes this argument visible.

### milestone plan

#### completed milestones

`v0.3.x`: ROS2 thin adaptor baseline landed

- released the first Ubuntu 22.04 + ROS 2 Humble transport path
- kept the public transport surface intentionally narrow: `(env.attach "ros2")`, `Odometry` in, `Twist` out
- established rosbag-backed `L2`, install/export coverage, and a ROS-enabled release artefact path

`v0.4.0`: replay and observability parity landed

- released direct canonical event-log output for ROS-backed runs
- released trace-level replay validation and one shared canonical artefact layout across simulator and ROS-backed runs
- documented canonical lifecycle anchors and ROS timing metadata in the released log surface

`v0.5.0`: same BT, different IO transport landed

- one shared wheeled flagship BT now runs across Webots, PyBullet, and ROS2 with adapter-only differences
- scripted comparison checks now cover both the cross-transport lane and the stricter same-robot PyBullet/Webots lane
- the supported ROS2 tutorial now walks the end-to-end path through canonical log validation
- the PyBullet e-puck-style surrogate and the Isaac wheeled ROS2 showcase now provide supporting demo and evidence lanes

`v0.6.0`: host capability bundles and planner boundary stabilisation landed

- host capability bundle boundaries are documented for future manipulation, navigation, and perception services
- `cap.motion.v1` and `cap.perception.scene.v1` are specified as host-level contracts, not released robot adapters
- `cap.list`, `cap.describe`, and `cap.call` provide the first registry/API path with deterministic `cap.echo.v1` smoke coverage
- the current decision for real runtime-affecting capability calls is to emit canonical `cap_call_start` and `cap_call_end` events once a real adapter lands, while keeping `cap.echo.v1` as a no-extra-event smoke path
- `planner.plan` request/result documentation now states success, timeout, error, fallback, budget, work cap, and `planner_v1` logging behaviour explicitly
- ROS2 `L2` remains focused on the released thin `Odometry` -> `Twist` lane because no concrete new ROS-backed path was added

Further work should now move to correctness hardening, runtime measurement, async evidence, model-backed services, host capability adapters, or concrete consumer fixes rather than casually broadening the baseline transport.

#### `v0.5.0`: same BT, different IO transport

Status:

- complete; retained here as the release record for scope and exit criteria

Focus:

- prove that muesli-bt semantics stay stable while the transport changes
- turn that result into one of the core evaluation evidence points, not just an integration smoke test

Scope:

- choose one canonical wheeled behaviour as the cross-transport flagship
- prefer a goal-seeking wheeled behaviour that can be expressed from pose, heading, velocity, goal, and bounded obstacle signals rather than backend-specific line or wall sensors
- use the existing Webots cluttered-goal path as the visual simulator baseline and align PyBullet and ROS2 around that same high-level behaviour
- run the same high-level BT through PyBullet, Webots, and ROS2 with adapter-only differences
- treat the current Webots e-puck versus PyBullet racecar pair as portability evidence, not as a same-robot decision-equivalence claim
- start a stricter comparison track by mirroring the Webots e-puck embodiment in PyBullet as a differential-drive surrogate
- document the common BT source and the transport-specific attach/config layer separately
- publish an improved integration tutorial that walks one supported backend path from attach/config through canonical log validation
- define a small set of scripted comparison checks for key behaviour, action, or decision-trace invariants across the three transports

Exit criteria:

- one flagship behaviour has a shared BT definition that is reused across simulator and ROS-backed runs
- the chosen flagship fits the released ROS2 `Odometry` -> `Twist` surface without introducing new ROS-specific sensors or message contracts
- docs point all three backends at the same BT logic, with only backend wiring changed
- one integration tutorial shows the supported backend flow end-to-end, including canonical log validation
- at least one scripted check compares key metrics or decision-trace invariants across the three transports
- the ROS2 story is now clearly “same BT, different IO transport”, not “special ROS-only behaviour”
- the cross-transport comparison is strong enough to support a release claim about stable semantics across transport changes

#### `v0.6.0`: host capability bundles and planner boundary stabilisation

Status:

- complete in `v0.6.0`; retained here as the release record for scope, exit criteria, and follow-up boundaries

Evidence:

- release notes: [v0.6.0](releases/v0.6.0.md)
- host boundary: [host capability bundles](integration/host-capability-bundles.md)
- manipulation contract: [cap.motion.v1](integration/cap-motion-v1.md)
- perception contract: [cap.perception.scene.v1](integration/cap-perception-scene-v1.md)
- planner contract: [`planner.plan`](planning/planner-plan.md)
- capability API docs: [`cap.list`](language/reference/builtins/cap/cap-list.md), [`cap.describe`](language/reference/builtins/cap/cap-describe.md), and [`cap.call`](language/reference/builtins/cap/cap-call.md)
- capability smoke coverage: `test_capability_registry_and_echo_call` in `tests/test_main.cpp`

Focus:

- keep the core/runtime planner story clean while preparing for richer external services
- define how future ROS libraries fit without becoming accidental BT semantics
- tighten the claim that bounded-time planning is stable, observable, and backend-agnostic

Scope:

- define the host capability bundle model for external execution, navigation, and perception services
- keep `env.*` as the direct transport surface
- keep `planner.plan` as the in-runtime bounded decision planner
- document that higher-level ROS libraries such as MoveIt and Nav2 belong behind separate host capability contracts
- define the first target capability families for manipulation and perception, but keep the public contracts generic rather than MoveIt-named or detector-named
- freeze the user-facing planner request/result contract for the evaluation baseline
- keep existing planner and ROS-backed conformance evidence unchanged unless a concrete new ROS-backed planner, capability, transport, or failure-mode path needs coverage
- keep planner time-budget behaviour explicit in docs and logs

Exit criteria:

- capability bundle registration and naming rules are documented without changing BT or Lisp semantics
- at least one manipulation contract and one perception contract are specified at the host level without hard-coding ROS library names into core semantics
- the split between `env.*`, `planner.plan`, and external host capabilities is explicit in docs and examples
- planner request/result docs match released behaviour and fixtures
- planner documentation covers the evaluation-critical success, timeout, error, fallback, `budget_ms`, `work_max`, and logging paths
- ROS2 `L2` remains focused on the released thin `Odometry` -> `Twist` lane until a concrete new ROS-backed path is added

#### `v0.7.0`: core defensibility and async correctness

Status: released in `v0.7.0`; release notes live in [v0.7.0 release notes](releases/v0.7.0.md).

Status vocabulary for this milestone:

- released: async cancellation lifecycle evidence, ROS-level pre-emption fixture, and `cap.echo.v1` registry smoke coverage
- experimental: Lisp DSL round-trip/hash evidence and generated-fragment rejection fixtures
- contract-only: host capability bundle contracts
- planned: real capability lifecycle events, production VLA providers, generated subtree execution, Nav2 adapters, and MoveIt adapters

Focus:

- close the most obvious rejection gap: a custom Lisp runtime inside C++ must not look like an unmeasured real-time liability
- finish the correctness story for cancellable async work
- make the runtime contract measurable through `mbt.evt.v1`, not only described in prose
- make the core engine usable and debuggable without a visual tool: errors, blackboard access, capability validation, and runtime diagnostics must be understandable from CLI output and canonical logs

Scope:

- add a tick hot-path audit mode that emits the [tick audit record](observability/tick-audit.md), including allocation count, allocation bytes, heap live bytes, GC deltas, tick id, node path, and logging mode for every tick
- add a strict benchmark and test mode where allocations during precompiled BT ticks fail the test, except for explicitly whitelisted logging paths outside the tick critical section
- pre-intern symbols and reuse node-state, blackboard, scheduler, and logging buffers where needed to make the common tick path allocation-free after warm-up
- add GC lifecycle telemetry for `gc_begin`, `gc_end`, forced collection, heap snapshots, mark time, sweep time, total pause time, live object count, freed object count, and triggering reason
- add a runtime GC policy switch for at least `default`, `between-ticks`, `manual`, and `fail-on-tick-gc`
- extend deterministic replay checks to cover async submit, poll, cancel, timeout, late completion, dropped completion, planner timeout, fallback, blackboard deltas, and host-action validation outcomes
- add a compact runtime outcome taxonomy for evaluation traces: `tick_ok`, `tick_deadline_missed`, `planner_timeout`, `vla_timeout`, `host_action_invalid`, `fallback_used`, `fallback_failed`, `late_result_dropped`, `cancel_acknowledged`, and `cancel_late`
- complete runtime-level async cancellation edge coverage, including cancellation before start, cancellation while running, cancellation after timeout, late completion after cancellation, and repeated cancellation
- add one ROS-level cancellation or pre-emption scenario only if it reuses the same runtime events and does not introduce a separate ROS-specific cancellation model
- document explicitly that `muesli-bt` is a task-level control runtime, not a hard real-time servo-loop runtime

Additional scope: engine diagnostics and usability baseline:

- add structured diagnostics for common user mistakes:
  - Lisp parse error with file, line, column, and nearby source context
  - unknown BT node type
  - malformed BT node arity or shape
  - unknown condition/action callback
  - unknown blackboard key
  - blackboard type mismatch
  - missing required blackboard key
  - unknown host capability
  - invalid capability payload
  - missing timeout for long-running async/model/planner calls where policy requires one
  - missing fallback where a fallback policy is required
  - rejected generated DSL fragment
- expose diagnostics as both human-readable CLI error text and structured canonical diagnostic events or validation reports
- use canonical diagnostic event families such as `engine_diagnostic.v1`, `bt_validation_error.v1`, `blackboard_error.v1`, `capability_validation_error.v1`, and `dsl_validation_error.v1`, or existing equivalents if they fit the schema better
- add a minimal documented blackboard/schema contract for examples and capability calls, covering required keys, optional keys, simple value types, defaults where appropriate, readable missing-key or wrong-type errors, and schema name or hash in logs where practical
- if Lisp syntax for blackboard schemas is too much for `v0.7.0`, start with a host-side schema declaration and validation command; the important user-facing result is that users can see what data a BT expects
- extend the host capability registry direction so `cap.describe` exposes request and result shape where available
- make `cap.call` reject malformed payloads before dispatch, with failures that include capability name, missing field, bad type, or unsupported value
- record capability validation success/failure in canonical logs where practical
- add a CLI validation path, such as `muslisp --check path/to/safe_goal.lisp` and optionally `muslisp --check path/to/safe_goal.lisp --schema path/to/safe_goal.yaml`, that validates BT source without connecting to a simulator or robot
- make the check path catch Lisp syntax errors, BT shape errors, unknown node types, missing required metadata, unavailable capabilities when a capability manifest is supplied, and timeout/fallback policy violations
- add a headless tree-introspection export for external tools, owned by the engine rather than `muesli-studio`
- include minimum export artefacts or equivalents for `bt_definition.v1`, `bt_instance.v1`, `node_metadata.v1`, and `blackboard_schema.v1` when available
- include tree id, node ids, node type, source location where available, canonical DSL hash, child relationships, capability or callback names used by leaves, and declared blackboard keys where known

Additional scope: Lisp DSL defensibility baseline:

- document the intended role of Lisp as a structured BT DSL rather than arbitrary scripting inside the control loop
- add a short `why Lisp as DSL?` page that explains the tree-as-data argument, the safety boundary, and the difference between generated BT fragments and host-side robot code
- add a canonical DSL round-trip check for representative BTs: source DSL -> parsed form -> compiled `bt_def` -> canonical DSL -> parsed form -> compiled `bt_def`, with stable structure or an explicitly documented normalisation difference
- add source hash and canonical DSL hash support where practical, so generated or loaded BT definitions can be identified in logs and replay reports
- add tests for `bt.to-dsl`, `bt.save-dsl`, and `bt.load-dsl` on representative sequence, selector, reactive, planner, and async/VLA-shaped trees, if these paths are already part of the public surface
- add validation fixtures for rejected generated fragments: unknown node type, unknown host callback, unsupported capability, missing fallback around a long-running async/model call, invalid budget, and malformed subtree shape
- keep this work within the existing runtime contract and validation tooling; do not introduce a second macro system or a general-purpose plugin language for `v0.7.0`

Benchmark and evidence requirements:

- report per-tick allocation count and allocation bytes under representative BTs, with and without canonical logging enabled
- report GC pause distributions, including p50, p95, p99, and p999 pause times under long-running synthetic missions
- report tick latency distributions under no-GC, between-ticks-GC, and forced-GC-pressure conditions
- report deadline miss rate, fallback activation count, late-completion count, and dropped-completion count under deterministic injected async delays
- add a long-run memory benchmark that reports resident-set-size slope, heap-live-byte slope, and event-log size per tick
- add `B8` async contract benchmarks for cancellation before start, cancellation while running, cancellation after timeout, repeated cancellation, and late completion after cancellation
- make every `v0.7.0` benchmark emit an experiment manifest with compiler, build type, platform, CPU, runtime flags, scenario seed, and trace schema version
- report DSL round-trip pass/fail counts for representative BT shapes
- include at least one canonical log where the BT definition is identified by source hash or canonical DSL hash
- include at least one negative fixture where a generated fragment is rejected before execution and the rejection is logged or reported deterministically
- report validation time for representative BT source files
- report diagnostic coverage count for known invalid fixtures
- add blackboard schema validation success/failure fixtures
- add capability payload validation success/failure fixtures
- add a tree-introspection export round-trip check

Exit criteria:

- precompiled core BT execution has a measured zero-allocation hot path in strict audit mode, or any remaining allocation is explicitly documented and justified
- GC during tick is either forbidden in strict mode or logged as a contract violation
- async submit, poll, cancel, timeout, and late-completion-drop behaviour are all covered by deterministic tests or fixtures
- canonical logs show the required cancellation, timeout, fallback, and late-result lifecycle events for the supported cases
- replay validation reports first divergence precisely, including divergent tick index, node id, blackboard key, async job id, or host capability call where applicable
- any ROS-level cancellation coverage reuses the runtime semantics and does not introduce a second cancellation model
- the release can generate at least one reproducible tail-latency figure and one memory/GC figure from checked-in scripts
- the docs state clearly that Lisp is used as a compact structured BT representation, not as an excuse to run arbitrary unbounded code inside the tick loop
- representative BTs can be serialised to canonical DSL and loaded again without changing intended execution semantics
- invalid generated BT fragments are rejected before execution by deterministic validation tests
- the release has enough DSL round-trip and validation evidence to support the later `v0.9.0` generated-subtree demonstration
- common user mistakes produce actionable errors rather than generic Lisp/runtime failures
- a BT source file can be checked without connecting to a simulator or robot
- representative BTs emit enough tree metadata for external tools to reconstruct the tree structure
- blackboard and capability validation failures are deterministic and covered by tests
- no feature in this engine-usability section requires `muesli-studio` to understand what went wrong

GC contract evidence:

- `test_tick_audit_marks_in_tick_gc_as_violation` forces a GC inside a tick under the default policy and checks that `gc_begin` and `gc_end` carry `"in_tick":true` while `tick_audit` reports `"violation":"tick_gc"`
- `test_fail_on_tick_gc_prevents_in_tick_gc_lifecycle` forces the same path under `fail-on-tick-gc` and checks that the runtime logs the rejection while no in-tick GC lifecycle event is emitted
- `test_strict_gc_representative_ticks_have_zero_gc_delta` runs representative sequence, selector, and reactive shapes under `fail-on-tick-gc` and checks that every audit row reports `"strict_gc":true`, `"gc_policy":"fail-on-tick-gc"`, and `"gc_collections_delta":0`

Replay divergence reporting evidence:

- `tools/validate_trace.py compare` emits `comparison.first_divergence` with the divergent event index, tick, event type, field path, raw event context windows, and any available node id, blackboard key, async job id, planner id, or host capability.
- `tests/check_validate_trace.py` covers node, blackboard, async job, and host capability divergence reports.

Outcome taxonomy evidence:

- the canonical schema accepts compact `runtime_outcome.v1` events for `tick_ok`, `tick_deadline_missed`, `planner_timeout`, `vla_timeout`, `host_action_invalid`, `fallback_used`, `fallback_failed`, `late_result_dropped`, `cancel_acknowledged`, and `cancel_late`
- the BT runtime emits the implemented tick, planner timeout, VLA timeout, late-result-drop, and cancellation outcome events alongside the detailed lifecycle events
- the ROS2 `L2` rosbag corpus includes a safe-action pre-emption case that reuses `host_action_invalid` and `fallback_used`; it does not add a ROS-specific cancellation model

#### `v0.8.0`: model-backed async and VLA stress path

Focus:

- move VLA support from stubbed orchestration to a real model-backed asynchronous service
- prove that deadlines, cancellation, stale-result rejection, and fallback matter under model latency
- keep the model integration behind host capability contracts rather than turning VLA into a special runtime case
- make asynchronous model/VLA integration look like ordinary host-capability use, so the BT source is independent of whether a service runs in-process, as a subprocess, on an edge server, through ROS2, or over HTTP

Scope:

- implement at least one real model backend behind the existing VLA service interface, preferably as a local process or HTTP backend that can be replayed deterministically from stored request and response records
- integrate VLA as a transport-transparent asynchronous host capability, using SmolVLA through the LeRobot async inference path as the primary practical backend and OpenVLA-OFT as the heavyweight VLA backend; the BT source must remain independent of whether inference runs locally, on an edge server, over HTTP/ROS2, or from a replay cache
- keep the current stub backend as a deterministic unit-test backend, not as the release evidence path
- support submit, poll, timeout, cancellation, partial response, final response, response hashing, request hashing, replay from stored records, and backend failure reporting
- define first capability names for model-backed perception or action proposal, for example `cap.perception.scene.v1`, `cap.vla.select_target.v1`, or `cap.vla.propose_nav_goal.v1`
- implement an injected latency and failure layer for VLA and planner backends, including delayed success, timeout, invalid action schema, unsafe action value, stale scene timestamp, dropped response, backend crash, and cancellation ignored until completion
- implement first-class action validation before any VLA or planner output can reach `env.act` or a host capability execution call
- include validators for continuous bounds, max command delta, timestamp freshness, target frame validity, forbidden zones, Nav2 goal validity where available, and host-declared capability schema compliance
- extend canonical logs with `vla_submit`, `vla_partial`, `vla_result`, `vla_cancel`, `vla_timeout`, `action_validation`, and `model_result_dropped` events, or their existing canonical equivalents if already named differently
- keep the flagship wheeled demo polished, but make the evaluation focus in this milestone the model-latency and cancellation behaviour, not visual demo quality
- keep the existing Isaac Sim / ROS2 showcase as supporting evidence, not as a required semantic surface or CI dependency

Additional scope: capability authoring and backend usability:

- add a small but complete developer path for adding a new host capability
- include public capability authoring material such as:

```text
include/muesli_bt/capability_api.hpp      # or equivalent public header
examples/capabilities/minimal_action/
examples/capabilities/http_backend/
docs/tutorials/writing-a-host-capability.md
docs/reference/capability-authoring.md
```

- show how to define a capability name, request schema, result schema, registration path, execution implementation, request validation, structured success/error/timeout outcomes, canonical event emission, and replay from recorded request/result data where relevant
- include both a synchronous toy capability and an asynchronous capability
- make the VLA/model backend path explicitly transport-transparent: BTs should depend on capability name, request schema, result schema, deadline, validation rule, and fallback policy, not HTTP, gRPC, subprocess layout, ROS2 actions, cloud endpoints, GPU placement, or credentials
- configure backend placement outside BT syntax, for example:

```yaml
capabilities:
  cap.vla.propose_nav_goal.v1:
    backend: http
    endpoint: http://edge-gpu.local:8080/v1/propose_nav_goal
    timeout_ms: 500
    replay_cache: runs/vla-cache/
```

and:

```yaml
capabilities:
  cap.vla.propose_nav_goal.v1:
    backend: local_process
    command: ./serve_vla --model example-model
    timeout_ms: 500
```

- make the same BT runnable against different backend configurations where feasible
- add a documented capability configuration loader with backend type, endpoint or command, timeout defaults, retry policy where supported, replay cache path, schema path, validation policy, and redaction policy for sensitive fields
- validate capability configuration before runtime execution
- expose validation as a reusable engine concept for VLA/model outputs, planner outputs, host capability results, generated BT fragments, and command outputs before `env.act` where appropriate
- provide validation result objects with valid/invalid status, reason code, field path, source capability or node, selected fallback, and whether the result was allowed to reach the host
- add a standard replay record format for asynchronous capability calls, including request hash, request summary, response hash, response summary, backend identity, latency, deadline, validation outcome, cancellation outcome, stale-result status, and redaction markers where needed

See also: [VLA backend integration plan](integration/vla-backend-integration-plan.md).

The VLA backend integration plan is a temporary planning document. Once the SmolVLA/OpenVLA-OFT path is implemented, documented through normal user-facing pages, covered by fixtures, and represented in release evidence, delete the planning page and replace roadmap links with the implementation docs and evidence pages.

Additional scope: constrained model or template-produced BT fragments:

- keep the main `v0.8.0` model-backed evidence focused on asynchronous model calls, stale-result rejection, validation, and fallback
- add an optional low-risk path where a model backend or deterministic template proposes a BT fragment as Lisp DSL data, but only for validation and rejection/acceptance testing; execution of generated fragments can remain a `v0.9.0` requirement
- ensure model-proposed or template-produced fragments go through the same parser, normaliser, validator, capability checks, budget checks, and fallback checks as hand-authored fragments
- log generated-fragment metadata without treating model text as trusted code: source, source hash, canonical DSL hash, validation status, rejection reason, and associated async/model job id
- prefer deterministic template-produced fragments for the main CI path; use real model-produced fragments only as supporting evidence unless reproducibility is strong

Benchmark and evidence requirements:

- report VLA latency distributions under real backend execution and under seeded injected-latency profiles
- report stale completion rate, cancellation acknowledgement latency, late-result-drop count, invalid-action count, fallback count, and unsafe-action-to-host count
- report task success under at least three VLA/planner latency conditions: nominal, heavy-tail latency, and failure-injected latency
- report the effect of runtime validation by comparing invalid model outputs produced, invalid model outputs rejected, and invalid model outputs reaching the host, which should be zero for the supported path
- include one trace bundle where a model result arrives late and is correctly dropped rather than committed
- include accepted and rejected generated-fragment examples in the trace corpus
- include at least one rejection caused by an unsafe or unsupported host capability request
- include at least one rejection caused by a missing timeout, missing fallback, or invalid budget around a long-running model/planner call
- include one synchronous custom capability example
- include one asynchronous custom capability example
- include one HTTP or local-process model/VLA backend example
- run the same BT against at least two backend configurations if feasible
- include invalid capability request and invalid capability result fixtures
- include a replayed async capability fixture

Exit criteria:

- at least one real model backend is used in a reproducible demo or benchmark
- all VLA and planner outputs pass through a documented validator before execution
- seeded latency and failure injection is reproducible across runs
- the same VLA or planner fault schedule can be replayed from trace artefacts
- the release can generate a reproducible “tail latency under injected model lag” figure
- the release can generate a reproducible “stale result, cancellation, and fallback outcomes” table
- the wheeled flagship demo remains reproducible with canonical log validation and does not depend on simulator-specific semantics
- generated BT fragments are never executed directly from raw model text
- every accepted fragment has a canonical DSL form and hash
- every rejected fragment has a deterministic rejection reason
- the model/VLA path strengthens the Lisp-as-DSL argument without depending on LLM-generated control logic
- a new user can add a simple host capability by following one tutorial
- model/VLA execution is configured outside the BT source
- the same BT can use different backend placements without source changes
- the same BT source can run against at least one live VLA backend and one replay backend by changing only capability configuration
- capability request and result validation failures produce useful diagnostics
- async capability calls are replayable from recorded request/result metadata
- no visual tool is required to understand capability registration, validation, or failure

#### `v0.9.0`: baselines, ROS capability bridges, and evaluation hardening

Focus:

- prevent the evaluation from looking like an implementation report with only internal benchmarks
- build a fair comparison path against BehaviorTree.CPP
- add the ROS2 glue needed for a physical or physical-like validation run without contaminating core runtime semantics
- reduce the risk that the evidence rests on one example only
- make `muesli-bt` usable as an engine inside a normal robot software stack, not only as a standalone executable or benchmark harness

Scope:

- add a neutral scenario description format that can emit both `muesli-bt` Lisp and BehaviorTree.CPP XML for matched experiments
- generate or provide equivalent BehaviorTree.CPP custom node skeletons for async planner/model calls, polling, timeout, and halt/pre-emption
- make the BehaviorTree.CPP baseline a strong baseline, using non-blocking/stateful action nodes and proper halt handling rather than blocking actions inside `tick`
- add a neutral comparison event schema or adapter layer so both systems can report tick start/end, node status, async submit, async cancel, async result, deadline miss, stale commit, fallback, and action commit events
- extend the benchmark harness beyond microbenchmarks with contract benchmarks for async timeout, late cancellation, invalid action, hot subtree swap, replay parity, VLA latency sweep, ROS callback pressure, and long-run memory behaviour
- implement a Nav2 host capability adapter as the main ROS2 bridge for the wheeled demo, with capability surfaces for navigate-to-pose, follow-waypoints if needed, cancel-goal, status, and clear-costmaps if needed
- map Nav2 action feedback, result codes, cancellation acknowledgement, transform age, and planner/controller timing into canonical host capability events
- keep the existing thin `Odometry` -> `Twist` lane as the baseline transport path; Nav2 is a host capability bundle, not a replacement for `env.*`
- add a physical-demo runbook for a TurtleBot3-style or equivalent differential-drive platform, including required ROS distro, launch files, map, BT source hash, model backend, fault seed, rosbag, `events.jsonl`, replay report, and video/time-alignment artefacts
- add a second serious scenario only if it can be kept reproducible; preferred candidate remains Towers of Hanoi with a simulated robot arm, but a simpler manipulator benchmark is acceptable if Hanoi becomes too heavy
- treat MoveIt as the first concrete manipulation host capability bundle if the non-wheeled scenario proceeds
- add a perception scene normaliser before BT logic consumes perception outputs, preferably using a YOLO-compatible detector or another reproducible detector path feeding stable scene state rather than raw detections
- choose Isaac Sim, Webots + ROS, or another simulator for the non-wheeled path based on reproducibility, MoveIt compatibility, and perception support, not appearance

Additional scope: practical ROS2, embedding, and safety expectations:

- add a pattern for writing ROS2-backed capabilities in addition to the specific Nav2 adapter
- keep ROS2 capability examples as wrappers, not a second semantic layer
- cover practical wrappers users expect:
  - ROS2 action client capability
  - ROS2 service client capability
  - ROS2 topic subscriber state feed
  - ROS2 publisher or command capability
  - lifecycle/shutdown handling
  - timeout and cancellation mapping
  - canonical event mapping
- include artefacts such as:

```text
integrations/ros2_examples/action_capability/
integrations/ros2_examples/service_capability/
docs/tutorials/writing-a-ros2-capability.md
```

- add a minimal external C++ embedding example showing whether `muesli-bt` is only `muslisp` or can be used as a library
- include artefacts such as:

```text
examples/embedding/cpp_minimal/
docs/tutorials/embedding-muesli-bt.md
```

- show how to create a runtime, load/compile a BT, register callbacks or capabilities, set blackboard values, run a tick loop, emit an event log, and shut down cleanly
- add a practical BT authoring cookbook for engine-level patterns, such as `docs/tutorials/bt-authoring-cookbook.md`
- cover guarded action, retry with timeout, async action with cancellation, planner call with fallback, VLA/model request with validation, Nav2 goal with cancel, blackboard read/write pattern, subtree reuse, generated guarded recovery subtree, and safe stop / last-safe-action fallback
- start a public API stability report before `v1.0.0`, such as `docs/project/api-compatibility.md`
- identify stability for the Lisp BT DSL surface, event schema, capability registry API, capability backend API, blackboard/schema validation API, embedding API, ROS2 adapter API where present, and generated-subtree validation/install API where released
- add task-runtime safety hooks that are not replacements for low-level robot safety:
  - heartbeat/watchdog event
  - command age limit
  - last-safe-command policy
  - safe-stop fallback policy
  - capability unavailable policy
  - stale observation policy
  - shutdown fallback policy
- document these safety hooks as host/runtime boundary hooks

Lisp-specific evidence path: generated guarded recovery subtree:

- add one evaluation demonstration where Lisp clearly enables useful runtime task-logic handling beyond hand-written BT execution
- recommended demo: `generated guarded recovery subtree`
- start with a normal wheeled goal-seeking BT used in the existing cross-transport path
- when a deterministic blocked-path or degraded-model condition is observed, generate a guarded recovery subtree as Lisp data using either quasiquote/templates or a deterministic generator
- the generated subtree should include at least:
  - a precondition guard
  - a bounded async planner or model call
  - a timeout
  - an explicit fallback action
  - a host capability call or `env.*` action that already belongs to the released support surface
  - a recovery exit condition
- validate the generated subtree before execution against:
  - BT grammar
  - known node types
  - known host callbacks or capabilities
  - action schema
  - budget and timeout constraints
  - required fallback policy
  - optional capability availability from `cap.list` / `cap.describe`
- compile the validated subtree into a `bt_def`
- install the subtree only at a tick boundary
- failed validation must leave the old tree active and emit a canonical rejection event
- successful installation must emit canonical events that identify the old subtree hash, new source hash, new canonical DSL hash, validation result, compile time, validate time, install tick, and install mode
- save the canonical generated DSL form as an artefact so that the same subtree can be inspected, diffed, reloaded, and replayed
- add a replay fixture proving that a run containing the generated subtree can be validated and compared with first-divergence reporting
- include a small comparison against a non-generated fixed-recovery BT to show what the dynamic path adds; this comparison should be framed as evidence of runtime task-logic handling, not as a claim that Lisp is universally superior

Implementation artefacts:

Add or equivalent:

```text
generated_guarded_recovery.lisp under examples/bt/
docs/tutorials/generated-guarded-recovery.md
docs/getting-oriented/why-lisp-dsl.md
docs/evidence/lisp-dsl-generated-subtree.md
tools/validate_generated_bt_fragment.py       # if validation is Python-side
# or equivalent C++/runtime validation command if implemented there
fixtures/dsl/generated_guarded_recovery/
```

The exact file names may change, but the release should contain:

- one runnable example
- one tutorial
- one evidence page
- one trace fixture
- one generated canonical DSL artefact
- one rejection fixture
- one replay/compare command

The `docs/getting-oriented/why-lisp-dsl.md` page should include at least:

```md
# why Lisp as the BT DSL?

`muesli-bt` uses Lisp because Behaviour Trees are tree-structured task logic, and Lisp S-expressions are compact structured tree data.

This lets one representation serve several roles:

- hand-authored BT source;
- generated BT fragment;
- validation input;
- canonical serialisation format;
- replay artefact;
- safe runtime patch format.

The Lisp layer is not a replacement for robot drivers, ROS2, Nav2, MoveIt, or hard real-time control. Host capabilities own robot IO and safety-critical execution. The Lisp DSL owns task-level decision structure under the runtime contract.
```

The page should include the generated guarded recovery demo as the main example.

Canonical events for the demo:

Add canonical events or documented equivalents for:

```text
dsl_fragment_generated
dsl_fragment_normalised
dsl_fragment_validation_ok
dsl_fragment_validation_failed
dsl_fragment_compiled
subtree_install_requested
subtree_installed
subtree_install_rejected
subtree_replay_loaded
```

Each event should include enough context for replay and evidence artefacts:

- run id
- tick id where applicable
- tree id
- old subtree hash
- new source hash
- new canonical DSL hash
- validation rule set
- rejection reason, if applicable
- compile latency
- validation latency
- install mode
- associated async/model/planner job id, if generated from such a job

Benchmark and evidence requirements:

- report matched `muesli-bt` versus BehaviorTree.CPP results for tick latency, deadline miss rate, stale-result count, fallback count, cancellation acknowledgement latency, and trace completeness under identical scenario seeds
- report generated-tree or live-patch compile, validate, and swap latency versus subtree size
- report Nav2 goal cancellation latency, recovery time after a blocked path, transform age distribution, and deadline miss rate while ROS callbacks are active
- if the physical robot path is available, report success rate, recovery latency, stale-result suppression count, fallback count, and failure classifications across repeated runs
- if the manipulator path is available, report planning latency, stale-scene rejection, trajectory validity, execution cancellation latency, and task success
- keep every comparison reproducible from a checked-in manifest, not from hand-run shell history
- report generation, normalisation, validation, compile, and tick-boundary install latency versus subtree size
- report accepted versus rejected generated fragments across deterministic fixtures
- report replay parity for at least one run containing an installed generated subtree
- report first-divergence behaviour when the generated subtree artefact is deliberately changed
- report task outcome for fixed-recovery BT versus generated guarded recovery BT under the same blocked-path or degraded-model scenario
- report any additional allocations caused by generated-subtree installation separately from the normal tick hot path
- build one external C++ embedding example in CI
- build/test one ROS2 action-capability example or Nav2 capability example where ROS2 CI is available
- cover one cookbook pattern with a runnable example
- include one watchdog or stale-observation fixture
- include one safe-stop or last-safe-command fallback fixture
- include one compatibility report listing stable and unstable APIs

Exit criteria:

- at least one fair BehaviorTree.CPP baseline exists for a matched scenario and is runnable from documented commands
- at least one contract benchmark shows behaviour that is not reducible to ordinary mean tick speed
- at least one generated or live-patched subtree experiment demonstrates why a Lisp runtime is useful beyond implementation taste
- the Nav2 adapter can run the wheeled scenario through a host capability boundary and emit canonical logs
- one ROS-backed row or slice exists in the evaluation outputs, preferably Nav2-backed; if not physical, it must at least be rosbag-backed and replay-validated
- two distinct scenarios exist with clear rationale, or the roadmap records why the second scenario was deferred to preserve the core evaluation claim
- all baseline and ROS evidence uses the same canonical event-log discipline as the core runtime experiments
- at least one generated guarded recovery subtree demonstration is runnable from documented commands
- the demonstration proves the full Lisp-as-DSL path: generate as Lisp data, normalise, validate, compile, serialise, install at tick boundary, execute, log, and replay
- invalid generated fragments are rejected before execution and leave the previous tree active
- accepted generated fragments are identified in logs by canonical DSL hash
- the documentation explains why this demonstration depends on Lisp as structured tree data rather than treating Lisp as a cosmetic syntax choice
- the evidence artefact set includes at least one figure, table, or trace excerpt from this demonstration
- users can see how to embed `muesli-bt` as an engine rather than only invoke `muslisp`
- users can see how to implement a ROS2-backed capability without changing core BT semantics
- common BT design patterns are documented as copyable recipes
- the release identifies which APIs are stable, experimental, or internal
- task-level safety hooks are documented and covered by at least one fixture
- none of these engine features require `muesli-studio`

#### `v1.0.0`: evidence artefacts and release baseline

Focus:

- cut the first release-ready, tool-builder-friendly, release-quality baseline
- make the `v1.0.0` release internally consistent across traces, scripts, manifests, and benchmark outputs
- make the `v1.0.0` release usable as a BT engine by early adopters, not only as an evidence artefact

Scope:

- freeze the release runtime contract, compatibility policy, event schema, conformance levels, benchmark manifests, and supported host capability contracts
- ensure `L0`, `L1`, and `L2` are documented with exact run commands, expected artefacts, and failure interpretation
- publish source release plus verified Ubuntu `x86_64`, Ubuntu ROS-enabled, and macOS `arm64` artefacts where still supported
- publish trace bundles for the core runtime, VLA/model-latency path, BehaviorTree.CPP comparison path, and ROS-backed evaluation path
- publish scripts that regenerate all release evaluation tables and figures from checked-in or archived artefacts
- include memory, GC, allocation, tail-latency, cancellation, fallback, replay, baseline, and ROS evidence in the release artefact set
- document the exact claim boundaries: task-level deadlines rather than hard real time, host capabilities rather than ROS semantics, and model orchestration rather than a new VLA model
- publish the Lisp-as-DSL technical argument in the README or documentation entry path, with careful wording that presents Lisp as structured BT data rather than arbitrary scripting
- publish the generated guarded recovery subtree demonstration as a supported evidence artefact or clearly labelled evaluation example
- include the generated canonical DSL artefacts, rejection fixtures, trace files, replay reports, and scripts needed to reproduce the demonstration
- ensure the generated-subtree demo is referenced from the evidence index, runtime contract material, and documentation path for new users
- ensure the release documentation supports the claim that Lisp can represent authored, generated, validated, serialised, replayed, and safely installed BT fragments
- freeze the supported `v1.0.0` engine surface explicitly:
  - Lisp BT DSL subset
  - runtime contract version
  - canonical event schema version
  - conformance levels
  - blackboard/schema validation behaviour
  - capability registry and call API
  - async capability lifecycle
  - validation result structure
  - embedding API, if released
  - ROS2 adapter support level
  - generated-subtree support level
  - benchmark/evidence artefact layout
- mark everything outside that surface as experimental or internal
- complete the practical user path so docs support users who need to build and run the engine, write a small BT, validate a BT before running it, define blackboard inputs, add custom and async host capabilities, configure model/VLA backends without changing BT source, embed the runtime in C++, connect a ROS2-backed capability, emit and validate canonical logs, replay or compare a run, and inspect the run in `muesli-studio` if desired
- keep visual inspection as a `muesli-studio` concern, while ensuring `muesli-bt` emits all canonical artefacts required for that inspection
- provide starter material that users can copy, such as:

```text
examples/starter/core_bt_project/
examples/starter/cpp_embedded_project/
examples/starter/custom_capability_project/
examples/starter/ros2_capability_project/
```

- keep starter projects minimal but buildable
- package engine artefacts cleanly, including executable, public headers if embedding is supported, CMake package config if library consumption is supported, schemas, example BTs, capability examples, validation tools, trace validation tools, compatibility docs, release notes, and evidence manifest
- define what remains outside the `v1.0.0` engine release unless already implemented: visual editor, timeline UI, live replay UI, graphical debugging, large-scale VLA provider management, hard real-time control, low-level safety controller, and broad ROS2 replacement layer

Exit criteria:

- runtime contract, compatibility policy, and conformance docs match the tagged code exactly
- `L0`, `L1`, and `L2` pass from clean checkout using documented commands
- hot-path allocation, GC pause, and long-run memory evidence are generated from the tagged codebase
- async cancellation, timeout, late-completion-drop, fallback, and action-validation behaviour are covered by deterministic fixtures and trace validation
- at least one real model-backed VLA or model-mediated async experiment is included, not only stub output
- the release includes replayable trace bundles for at least one SmolVLA-backed or OpenVLA-OFT-backed run, with model-call lifecycle events, validation outcomes, stale-result handling, and fallback behaviour visible in `mbt.evt.v1`
- at least one fair BehaviorTree.CPP baseline comparison is included with public baseline code and matched scenario manifests
- at least one figure, table row, or evaluation slice includes ROS-backed evidence
- preferred outcome: at least one Nav2-backed or physical wheeled run is included with `events.jsonl`, rosbag, replay report, and failure classification
- acceptable fallback: if physical hardware is not stable enough by the tag, the release includes a rosbag-backed Nav2 or equivalent ROS action-capability run and states the physical limitation plainly
- the core claim is demonstrably true: BT semantics stay stable while transport changes, asynchronous planner/model work is deadline-aware and cancellable, and canonical event logs support replay and inspection across supported transports
- the `v1.0.0` release contains one implemented demonstration where Lisp clearly enables useful runtime BT handling beyond hand-authored static trees
- the demonstration is reproducible from a clean checkout or archived release artefact using documented commands
- the demonstration includes at least one accepted generated fragment and at least one rejected unsafe or invalid generated fragment
- the generated fragment path is covered by canonical logs and replay validation
- the documentation states the Lisp argument explicitly and honestly: Lisp is used because BTs are tree-structured task logic and S-expressions give a compact inspectable representation for generation, validation, serialisation, replay, and tick-boundary installation
- the documentation also states the boundary: host-side robot IO, safety-critical control, and real-time servo loops remain outside the Lisp DSL
- a new user can implement and run a simple custom capability without reading the C++ internals
- a new user can validate a BT and understand common errors from CLI output
- a new user can define or inspect expected blackboard inputs for a BT
- a new user can run an asynchronous capability and see timeout, cancellation, validation, and fallback behaviour in logs
- a C++ user can embed the runtime using a documented minimal example, or the release explicitly states that embedding is not yet supported
- a ROS2 user can follow one documented capability-backed path without changing core runtime semantics
- all public APIs that users are expected to depend on are listed as supported, experimental, or internal
- `muesli-bt` emits sufficient canonical metadata for `muesli-studio` to visualise runs, but the engine does not depend on the Studio UI

## api / syntax

This roadmap uses the conformance levels as release gates:

- `L0`: core-only contract checks
- `L1`: simulator-backed conformance
- `L2`: ROS2 rosbag-backed conformance

This roadmap also uses the following release terms:

- `supported`: documented, tested in CI, and included in the release support posture
- `baseline`: good enough to claim as part of the public release surface
- `release-ready`: stable enough to cite directly in evidence artefacts and evaluation

## example

Example interpretation for `v0.5.0`:

If PyBullet, Webots, and ROS2 all run the flagship scenario, but each backend still needs its own BT source file, the work is not yet good enough for `v0.5.0`.

Example interpretation for `v1.0.0`:

If a ROS-backed demo works but the same BT cannot be shown across simulator and ROS transport, the deployability story is still incomplete.

If the VLA path still uses only the stub backend, the model-integration claim is not release-ready.

If the BehaviorTree.CPP comparison uses blocking action nodes or a weaker hand-written baseline, the comparison is not release-ready.

If the hot-path benchmark reports only mean tick time and not allocations, GC pauses, and tail latency, the Lisp runtime defensibility story is incomplete.

Example interpretation for the Lisp DSL evidence path:

If the release only shows hand-authored Lisp BTs, the Lisp argument is incomplete. The release must include at least one generated or transformed BT fragment that is validated, compiled, serialised, installed safely, logged, and replayed.

If generated fragments are executed directly from raw model text, the path is not acceptable. Generated fragments must pass through parser, normaliser, validator, capability checks, budget checks, and fallback checks before execution.

If live subtree installation can occur mid-tick, the path is not acceptable. Installation must occur at a tick boundary or another documented safe point.

Example interpretation for engine usability:

If users need `muesli-studio` to understand a BT validation failure, the engine diagnostics are incomplete. The CLI and canonical reports must explain the failure.

If a custom capability can only be added by copying internal source files or reading private implementation details, the capability authoring path is incomplete.

If a BT source file cannot be checked without connecting to a simulator or robot, the engine workflow is incomplete.

If model/VLA backend placement leaks into BT syntax, the capability abstraction is too weak. Backend placement belongs in validated configuration.

If `muesli-bt` emits traces that a tool can display only by reverse-engineering runtime internals, the metadata export is incomplete.

## gotchas

- ROS2 scope must not expand into a second semantic runtime.
- Flagship scenarios should use signals exposed by the released ROS2 baseline, not backend-specific shortcuts.
- Host capability bundles should stay narrow rather than becoming one catch-all ROS super-interface.
- Canonical `mbt.evt.v1` logs are the replay artefact of record.
- Demo polish is secondary to conformance and observability guarantees.
- Stub VLA output is deterministic fixture evidence, not evidence for model-integrated robotics.
- Real-time suitability depends on tail latency, allocation, GC, and long-run memory behaviour, not average tick speed alone.
- BehaviorTree.CPP comparisons need proper non-blocking actions and halt/pre-emption handling.
- Real model, Nav2, MoveIt, and Isaac integrations remain host capabilities. They must not add new Lisp or BT core semantics.
- The second flagship should follow the strongest reproducible evidence rather than being locked too early.
- Lisp is useful here as structured tree data for generated, validated, serialised, replayed, and safely installed BT fragments.
- Generated Lisp fragments are untrusted input until parsed, normalised, validated, and checked against capability and fallback rules.
- Generated-subtree demonstrations use the same runtime contract, canonical logging, and replay validation as hand-authored BTs.
- Dynamic subtree installation happens at a tick boundary or another explicitly documented safe point.
- Runtime task generation is safe only when validation, host capability contracts, action validation, fallback policy, and replayable event logs are in place.
- `muesli-bt` owns validation, diagnostics, metadata, logs, and replayable artefacts. `muesli-studio` may visualise and inspect them.
- Model/VLA placement and credentials belong in validated backend configuration, not BT syntax.
- Custom-capability authoring should not depend on undocumented internals.
- Task-level watchdogs and safe-stop hooks complement low-level robot safety systems; they do not replace them.
- Public API stability should be explicit before `v1.0.0`; mark APIs as supported, experimental, or internal.

## priority if scope has to be cut

If the remaining roadmap becomes too large, prioritise practical engine usability in this order:

1. structured diagnostics and `muslisp --check`
2. capability authoring tutorial and minimal SDK
3. blackboard/schema validation baseline
4. transport-transparent async capability backend config
5. embedding example
6. ROS2 action/service capability pattern
7. BT authoring cookbook
8. task-level safety hooks
9. starter project templates

## see also

- [Roadmap](limitations-roadmap.md)
- [todo](todo.md)
- [conformance levels](contracts/conformance.md)
- [runtime contract v1](contracts/runtime-contract-v1.md)
- [ros2 backend scope](integration/ros2-backend-scope.md)
- [cross-transport flagship for v0.5](integration/cross-transport-flagship.md)
