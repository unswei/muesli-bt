# roadmap to 1.0

## what this is

This page turns the current backlog, ROS2 scope, conformance work, and paper-oriented release planning into one release roadmap from the current baseline to `v1.0.0`.

This page is release-oriented. The shorter [Roadmap](limitations-roadmap.md) page remains the theme-based backlog.

## when to use it

Use this page when you:

- decide whether a change belongs before or after `v1.0.0`
- scope release work for `v0.5.0+`
- review whether ROS2 work is staying thin and bounded
- check what evidence is still missing for a paper-ready release

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

The current `main` branch also now carries the `v0.5.0` cross-transport flagship baseline:

- one shared wheeled BT reused across Webots, PyBullet, and ROS2
- a supported ROS2 tutorial from build through canonical log validation
- scripted cross-transport comparison checks
- a stricter same-robot comparison lane through the PyBullet e-puck-style surrogate
- supporting Isaac Sim / ROS2 wheeled showcase media and docs

This means the remaining path to `v1.0.0` is not “add ROS2 somehow” or “make replay work somehow”.
The thin adaptor and observability baseline are already real enough that the next question is how to exploit them without widening the semantic surface.

The remaining path is:

- preserve “same BT, different IO transport” as the first paper-facing evidence point
- harden the Lisp/C++ runtime so the paper can defend allocation behaviour, GC pauses, deadline handling, cancellation, and deterministic replay
- replace VLA stubs with at least one real model-backed asynchronous service and failure-injection path
- build a fair comparison engine against BehaviorTree.CPP rather than relying only on internal microbenchmarks
- add ROS2 host capability bridges, especially Nav2 and, if still feasible, MoveIt, without widening core runtime semantics
- keep the existing Isaac showcase as supporting evidence rather than a second semantic surface
- make the Lisp-as-DSL argument explicit and testable: Lisp is used as a compact structured representation for BTs, not as arbitrary scripting; before `v1.0.0`, include at least one implemented demonstration where a BT fragment is generated as Lisp data, validated, compiled, serialised, safely installed at a tick boundary, logged, and replayed
- finish paper-facing evaluation, trace bundles, and release hygiene

### paper-facing Lisp DSL argument

The paper and release documentation should defend Lisp as a technical choice, not as a matter of taste.

The argument is:

- Behaviour Trees are trees.
- Lisp S-expressions are executable structured tree data.
- A Lisp BT DSL can therefore serve as human-authored source, generated intermediate representation, validation input, serialisation format, replay artefact, and runtime patch format.
- This is useful for robotics because task logic can be generated or transformed without becoming opaque host code.
- The runtime can validate the generated tree against BT grammar, host capability contracts, budget rules, fallback requirements, and safety restrictions before it is allowed to execute.
- The same canonical event stream can record which generated tree was used, when it was validated, when it was installed, and how it behaved during replay.

The paper-facing claim should be narrow:

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

`v0.5.0`: same BT, different IO transport landed on `main`

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

- complete on `main`; retained here as the release record for scope and exit criteria

Focus:

- prove that muesli-bt semantics stay stable while the transport changes
- turn that result into one of the core paper evidence points, not just an integration smoke test

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
- the cross-transport comparison is strong enough to support a paper-facing claim about stable semantics across transport changes

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
- freeze the user-facing planner request/result contract for the paper baseline
- keep existing planner and ROS-backed conformance evidence unchanged unless a concrete new ROS-backed planner, capability, transport, or failure-mode path needs coverage
- keep planner time-budget behaviour explicit in docs and logs

Exit criteria:

- capability bundle registration and naming rules are documented without changing BT or Lisp semantics
- at least one manipulation contract and one perception contract are specified at the host level without hard-coding ROS library names into core semantics
- the split between `env.*`, `planner.plan`, and external host capabilities is explicit in docs and examples
- planner request/result docs match released behaviour and fixtures
- planner documentation covers the paper-critical success, timeout, error, fallback, `budget_ms`, `work_max`, and logging paths
- ROS2 `L2` remains focused on the released thin `Odometry` -> `Twist` lane until a concrete new ROS-backed path is added

#### `v0.7.0`: core defensibility and async correctness

Status: in progress on `main`; release-candidate notes live in [v0.7.0 release notes](releases/v0.7.0.md).

Status vocabulary for this milestone:

- released: async cancellation lifecycle evidence, ROS-level pre-emption fixture, and `cap.echo.v1` registry smoke coverage
- experimental: Lisp DSL round-trip/hash evidence and generated-fragment rejection fixtures
- contract-only: host capability bundle contracts
- planned: real capability lifecycle events, production VLA providers, generated subtree execution, Nav2 adapters, and MoveIt adapters

Focus:

- close the most obvious rejection gap: a custom Lisp runtime inside C++ must not look like an unmeasured real-time liability
- finish the correctness story for cancellable async work
- make the runtime contract measurable through `mbt.evt.v1`, not only described in prose

Scope:

- add a tick hot-path audit mode that emits the [tick audit record](observability/tick-audit.md), including allocation count, allocation bytes, heap live bytes, GC deltas, tick id, node path, and logging mode for every tick
- add a strict benchmark and test mode where allocations during precompiled BT ticks fail the test, except for explicitly whitelisted logging paths outside the tick critical section
- pre-intern symbols and reuse node-state, blackboard, scheduler, and logging buffers where needed to make the common tick path allocation-free after warm-up
- add GC lifecycle telemetry for `gc_begin`, `gc_end`, forced collection, heap snapshots, mark time, sweep time, total pause time, live object count, freed object count, and triggering reason
- add a runtime GC policy switch for at least `default`, `between-ticks`, `manual`, and `fail-on-tick-gc`
- extend deterministic replay checks to cover async submit, poll, cancel, timeout, late completion, dropped completion, planner timeout, fallback, blackboard deltas, and host-action validation outcomes
- add a compact runtime outcome taxonomy for paper-facing traces: `tick_ok`, `tick_deadline_missed`, `planner_timeout`, `vla_timeout`, `host_action_invalid`, `fallback_used`, `fallback_failed`, `late_result_dropped`, `cancel_acknowledged`, and `cancel_late`
- complete runtime-level async cancellation edge coverage, including cancellation before start, cancellation while running, cancellation after timeout, late completion after cancellation, and repeated cancellation
- add one ROS-level cancellation or pre-emption scenario only if it reuses the same runtime events and does not introduce a separate ROS-specific cancellation model
- document explicitly that `muesli-bt` is a task-level control runtime, not a hard real-time servo-loop runtime

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

Exit criteria:

- precompiled core BT execution has a measured zero-allocation hot path in strict audit mode, or any remaining allocation is explicitly documented and justified
- GC during tick is either forbidden in strict mode or logged as a contract violation
- async submit, poll, cancel, timeout, and late-completion-drop behaviour are all covered by deterministic tests or fixtures
- canonical logs show the required cancellation, timeout, fallback, and late-result lifecycle events for the supported cases
- replay validation reports first divergence precisely, including divergent tick index, node id, blackboard key, async job id, or host capability call where applicable
- any ROS-level cancellation coverage reuses the runtime semantics and does not introduce a second cancellation model
- the release can generate at least one paper-quality tail-latency figure and one memory/GC figure from checked-in scripts
- the docs state clearly that Lisp is used as a compact structured BT representation, not as an excuse to run arbitrary unbounded code inside the tick loop
- representative BTs can be serialised to canonical DSL and loaded again without changing intended execution semantics
- invalid generated BT fragments are rejected before execution by deterministic validation tests
- the release has enough DSL round-trip and validation evidence to support the later `v0.9.0` generated-subtree demonstration

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

Scope:

- implement at least one real model backend behind the existing VLA service interface, preferably as a local process or HTTP backend that can be replayed deterministically from stored request and response records
- keep the current stub backend as a deterministic unit-test backend, not as the paper-facing VLA evidence path
- support submit, poll, timeout, cancellation, partial response, final response, response hashing, request hashing, replay from stored records, and backend failure reporting
- define first capability names for model-backed perception or action proposal, for example `cap.perception.scene.v1`, `cap.vla.select_target.v1`, or `cap.vla.propose_nav_goal.v1`
- implement an injected latency and failure layer for VLA and planner backends, including delayed success, timeout, invalid action schema, unsafe action value, stale scene timestamp, dropped response, backend crash, and cancellation ignored until completion
- implement first-class action validation before any VLA or planner output can reach `env.act` or a host capability execution call
- include validators for continuous bounds, max command delta, timestamp freshness, target frame validity, forbidden zones, Nav2 goal validity where available, and host-declared capability schema compliance
- extend canonical logs with `vla_submit`, `vla_partial`, `vla_result`, `vla_cancel`, `vla_timeout`, `action_validation`, and `model_result_dropped` events, or their existing canonical equivalents if already named differently
- keep the flagship wheeled demo polished, but make the paper-facing novelty in this milestone the model-latency and cancellation behaviour, not visual demo quality
- keep the existing Isaac Sim / ROS2 showcase as supporting evidence, not as a required semantic surface or CI dependency

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

Exit criteria:

- at least one real model backend is used in a reproducible demo or benchmark
- all VLA and planner outputs pass through a documented validator before execution
- seeded latency and failure injection is reproducible across runs
- the same VLA or planner fault schedule can be replayed from trace artefacts
- the release can generate a paper-quality “tail latency under injected model lag” figure
- the release can generate a paper-quality “stale result, cancellation, and fallback outcomes” table
- the wheeled flagship demo remains reproducible with canonical log validation and does not depend on simulator-specific semantics
- generated BT fragments are never executed directly from raw model text
- every accepted fragment has a canonical DSL form and hash
- every rejected fragment has a deterministic rejection reason
- the model/VLA path strengthens the Lisp-as-DSL argument without making the paper depend on LLM-generated control logic

#### `v0.9.0`: baselines, ROS capability bridges, and evaluation hardening

Focus:

- prevent the paper from looking like an implementation report with only internal benchmarks
- build a fair comparison path against BehaviorTree.CPP
- add the ROS2 glue needed for a physical or physical-like validation run without contaminating core runtime semantics
- reduce the risk that the paper rests on one example only

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

Lisp-specific evidence path: generated guarded recovery subtree:

- add one paper-facing demonstration where Lisp clearly enables useful runtime task-logic handling beyond hand-written BT execution
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

Each event should include enough context for replay and paper artefacts:

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

Exit criteria:

- at least one fair BehaviorTree.CPP baseline exists for a matched scenario and is runnable from documented commands
- at least one contract benchmark shows behaviour that is not reducible to ordinary mean tick speed
- at least one generated or live-patched subtree experiment demonstrates why a Lisp runtime is useful beyond implementation taste
- the Nav2 adapter can run the wheeled scenario through a host capability boundary and emit canonical logs
- one ROS-backed row or slice exists in the evaluation outputs, preferably Nav2-backed; if not physical, it must at least be rosbag-backed and replay-validated
- two distinct scenarios exist with clear rationale, or the roadmap records why the second scenario was deferred to preserve the paper’s core claim
- all baseline and ROS evidence uses the same canonical event-log discipline as the core runtime experiments
- at least one generated guarded recovery subtree demonstration is runnable from documented commands
- the demonstration proves the full Lisp-as-DSL path: generate as Lisp data, normalise, validate, compile, serialise, install at tick boundary, execute, log, and replay
- invalid generated fragments are rejected before execution and leave the previous tree active
- accepted generated fragments are identified in logs by canonical DSL hash
- the documentation explains why this demonstration depends on Lisp as structured tree data rather than treating Lisp as a cosmetic syntax choice
- the paper artefact set includes at least one figure, table, or trace excerpt from this demonstration

#### `v1.0.0`: paper artefacts and release baseline

Focus:

- cut the first paper-ready, tool-builder-friendly, release-quality baseline
- make the tagged release match the paper exactly, including traces, scripts, manifests, and benchmark outputs

Scope:

- freeze the paper-facing runtime contract, compatibility policy, event schema, conformance levels, benchmark manifests, and supported host capability contracts
- ensure `L0`, `L1`, and `L2` are documented with exact run commands, expected artefacts, and failure interpretation
- publish source release plus verified Ubuntu `x86_64`, Ubuntu ROS-enabled, and macOS `arm64` artefacts where still supported
- publish trace bundles for the core runtime, VLA/model-latency path, BehaviorTree.CPP comparison path, and ROS-backed evaluation path
- publish scripts that regenerate all paper tables and figures from checked-in or archived artefacts
- include memory, GC, allocation, tail-latency, cancellation, fallback, replay, baseline, and ROS evidence in the release artefact set
- document the exact claim boundaries: task-level deadlines rather than hard real time, host capabilities rather than ROS semantics, and model orchestration rather than a new VLA model
- publish the Lisp-as-DSL technical argument in the README or documentation entry path, with careful wording that presents Lisp as structured BT data rather than arbitrary scripting
- publish the generated guarded recovery subtree demonstration as a supported paper artefact or clearly labelled paper-evidence example
- include the generated canonical DSL artefacts, rejection fixtures, trace files, replay reports, and scripts needed to reproduce the demonstration
- ensure the generated-subtree demo is referenced from the evidence index, runtime contract material, and documentation path for new users
- ensure the paper text can cite the release for the claim that Lisp supports authored, generated, validated, serialised, replayed, and safely installed BT fragments

Exit criteria:

- runtime contract, compatibility policy, and conformance docs match the tagged code exactly
- `L0`, `L1`, and `L2` pass from clean checkout using documented commands
- hot-path allocation, GC pause, and long-run memory evidence are generated from the tagged codebase
- async cancellation, timeout, late-completion-drop, fallback, and action-validation behaviour are covered by deterministic fixtures and trace validation
- at least one real model-backed VLA or model-mediated async experiment is included, not only stub output
- at least one fair BehaviorTree.CPP baseline comparison is included with public baseline code and matched scenario manifests
- at least one paper figure, table row, or evaluation slice includes ROS-backed evidence
- preferred outcome: at least one Nav2-backed or physical wheeled run is included with `events.jsonl`, rosbag, replay report, and failure classification
- acceptable fallback: if physical hardware is not stable enough by the tag, the release includes a rosbag-backed Nav2 or equivalent ROS action-capability run and states the physical limitation plainly
- the core claim is demonstrably true: BT semantics stay stable while transport changes, asynchronous planner/model work is deadline-aware and cancellable, and canonical event logs support replay and inspection across supported transports
- the tagged release contains one implemented demonstration where Lisp clearly enables useful runtime BT handling beyond hand-authored static trees
- the demonstration is reproducible from a clean checkout or archived release artefact using documented commands
- the demonstration includes at least one accepted generated fragment and at least one rejected unsafe or invalid generated fragment
- the generated fragment path is covered by canonical logs and replay validation
- the documentation states the Lisp argument explicitly and honestly: Lisp is used because BTs are tree-structured task logic and S-expressions give a compact inspectable representation for generation, validation, serialisation, replay, and tick-boundary installation
- the documentation also states the boundary: host-side robot IO, safety-critical control, and real-time servo loops remain outside the Lisp DSL

## api / syntax

This roadmap uses the conformance levels as release gates:

- `L0`: core-only contract checks
- `L1`: simulator-backed conformance
- `L2`: ROS2 rosbag-backed conformance

This roadmap also uses the following release terms:

- `supported`: documented, tested in CI, and included in the release support posture
- `baseline`: good enough to claim as part of the public release surface
- `paper-ready`: stable enough to cite directly in paper artefacts and evaluation

## example

Example interpretation for `v0.5.0`:

If PyBullet, Webots, and ROS2 all run the flagship scenario, but each backend still needs its own BT source file, the work is not yet good enough for `v0.5.0`.

Example interpretation for `v1.0.0`:

If a ROS-backed demo works but the same BT cannot be shown across simulator and ROS transport, the deployability story is still incomplete.

If the VLA path still uses only the stub backend, the model-integration claim is not paper-ready.

If the BehaviorTree.CPP comparison uses blocking action nodes or a weaker hand-written baseline, the comparison is not paper-ready.

If the hot-path benchmark reports only mean tick time and not allocations, GC pauses, and tail latency, the Lisp runtime defensibility story is incomplete.

Example interpretation for the Lisp DSL evidence path:

If the release only shows hand-authored Lisp BTs, the Lisp argument is incomplete. The release must include at least one generated or transformed BT fragment that is validated, compiled, serialised, installed safely, logged, and replayed.

If generated fragments are executed directly from raw model text, the path is not acceptable. Generated fragments must pass through parser, normaliser, validator, capability checks, budget checks, and fallback checks before execution.

If live subtree installation can occur mid-tick, the path is not acceptable. Installation must occur at a tick boundary or another documented safe point.

## gotchas

- Do not let ROS2 scope expand into a second semantic runtime.
- Do not choose a flagship that depends on backend-specific line sensors, wall sensors, or other signals that the released ROS2 baseline does not expose.
- Do not let “host capability bundles” turn into one giant catch-all ROS super-interface.
- Do not treat ad hoc replay artefacts as a replacement for canonical `mbt.evt.v1` logs.
- Do not use demo polish to hide missing conformance or observability guarantees.
- Do not present stub VLA output as evidence for model-integrated robotics.
- Do not present average tick speed as evidence of real-time suitability. Tail latency, allocation, GC, and long-run memory behaviour must be shown.
- Do not compare against a deliberately weak BehaviorTree.CPP baseline. The baseline must use proper non-blocking actions and halt/pre-emption handling.
- Do not let a real model, Nav2, MoveIt, or Isaac integration leak new semantics into the Lisp or BT core. They remain host capabilities.
- Do not lock the second flagship too early if the strongest reproducible evidence points elsewhere.
- Do not present Lisp as a virtue by itself. The argument is structured tree data for generated, validated, serialised, replayed, and safely installed BT fragments.
- Do not execute raw model-generated text as robot control logic. Treat generated Lisp fragments as untrusted input until parsed, normalised, validated, and checked against capability and fallback rules.
- Do not let the generated-subtree demonstration bypass the same runtime contract, canonical logging, or replay validation used by hand-authored BTs.
- Do not let dynamic subtree installation mutate the active tree during a tick. Use tick-boundary installation or another explicitly documented safe point.
- Do not claim that Lisp makes runtime task generation safe by itself. Safety comes from validation, host capability contracts, action validation, fallback policy, and replayable event logs.

## see also

- [Roadmap](limitations-roadmap.md)
- [todo](todo.md)
- [conformance levels](contracts/conformance.md)
- [runtime contract v1](contracts/runtime-contract-v1.md)
- [ros2 backend scope](integration/ros2-backend-scope.md)
- [cross-transport flagship for v0.5](integration/cross-transport-flagship.md)
