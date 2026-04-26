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
- finish paper-facing evaluation, trace bundles, and release hygiene

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

Benchmark and evidence requirements:

- report per-tick allocation count and allocation bytes under representative BTs, with and without canonical logging enabled
- report GC pause distributions, including p50, p95, p99, and p999 pause times under long-running synthetic missions
- report tick latency distributions under no-GC, between-ticks-GC, and forced-GC-pressure conditions
- report deadline miss rate, fallback activation count, late-completion count, and dropped-completion count under deterministic injected async delays
- add a long-run memory benchmark that reports resident-set-size slope, heap-live-byte slope, and event-log size per tick
- make every `v0.7.0` benchmark emit an experiment manifest with compiler, build type, platform, CPU, runtime flags, scenario seed, and trace schema version

Exit criteria:

- precompiled core BT execution has a measured zero-allocation hot path in strict audit mode, or any remaining allocation is explicitly documented and justified
- GC during tick is either forbidden in strict mode or logged as a contract violation
- async submit, poll, cancel, timeout, and late-completion-drop behaviour are all covered by deterministic tests or fixtures
- canonical logs show the required cancellation, timeout, fallback, and late-result lifecycle events for the supported cases
- replay validation reports first divergence precisely, including divergent tick index, node id, blackboard key, async job id, or host capability call where applicable
- any ROS-level cancellation coverage reuses the runtime semantics and does not introduce a second cancellation model
- the release can generate at least one paper-quality tail-latency figure and one memory/GC figure from checked-in scripts

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

Benchmark and evidence requirements:

- report VLA latency distributions under real backend execution and under seeded injected-latency profiles
- report stale completion rate, cancellation acknowledgement latency, late-result-drop count, invalid-action count, fallback count, and unsafe-action-to-host count
- report task success under at least three VLA/planner latency conditions: nominal, heavy-tail latency, and failure-injected latency
- report the effect of runtime validation by comparing invalid model outputs produced, invalid model outputs rejected, and invalid model outputs reaching the host, which should be zero for the supported path
- include one trace bundle where a model result arrives late and is correctly dropped rather than committed

Exit criteria:

- at least one real model backend is used in a reproducible demo or benchmark
- all VLA and planner outputs pass through a documented validator before execution
- seeded latency and failure injection is reproducible across runs
- the same VLA or planner fault schedule can be replayed from trace artefacts
- the release can generate a paper-quality “tail latency under injected model lag” figure
- the release can generate a paper-quality “stale result, cancellation, and fallback outcomes” table
- the wheeled flagship demo remains reproducible with canonical log validation and does not depend on simulator-specific semantics

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
- add live subtree patching as the Lisp-specific evidence path, with compile, validate, and swap only at tick boundaries; failed validation must leave the old tree active and emit a canonical event
- implement a Nav2 host capability adapter as the main ROS2 bridge for the wheeled demo, with capability surfaces for navigate-to-pose, follow-waypoints if needed, cancel-goal, status, and clear-costmaps if needed
- map Nav2 action feedback, result codes, cancellation acknowledgement, transform age, and planner/controller timing into canonical host capability events
- keep the existing thin `Odometry` -> `Twist` lane as the baseline transport path; Nav2 is a host capability bundle, not a replacement for `env.*`
- add a physical-demo runbook for a TurtleBot3-style or equivalent differential-drive platform, including required ROS distro, launch files, map, BT source hash, model backend, fault seed, rosbag, `events.jsonl`, replay report, and video/time-alignment artefacts
- add a second serious scenario only if it can be kept reproducible; preferred candidate remains Towers of Hanoi with a simulated robot arm, but a simpler manipulator benchmark is acceptable if Hanoi becomes too heavy
- treat MoveIt as the first concrete manipulation host capability bundle if the non-wheeled scenario proceeds
- add a perception scene normaliser before BT logic consumes perception outputs, preferably using a YOLO-compatible detector or another reproducible detector path feeding stable scene state rather than raw detections
- choose Isaac Sim, Webots + ROS, or another simulator for the non-wheeled path based on reproducibility, MoveIt compatibility, and perception support, not appearance

Benchmark and evidence requirements:

- report matched `muesli-bt` versus BehaviorTree.CPP results for tick latency, deadline miss rate, stale-result count, fallback count, cancellation acknowledgement latency, and trace completeness under identical scenario seeds
- report generated-tree or live-patch compile, validate, and swap latency versus subtree size
- report Nav2 goal cancellation latency, recovery time after a blocked path, transform age distribution, and deadline miss rate while ROS callbacks are active
- if the physical robot path is available, report success rate, recovery latency, stale-result suppression count, fallback count, and failure classifications across repeated runs
- if the manipulator path is available, report planning latency, stale-scene rejection, trajectory validity, execution cancellation latency, and task success
- keep every comparison reproducible from a checked-in manifest, not from hand-run shell history

Exit criteria:

- at least one fair BehaviorTree.CPP baseline exists for a matched scenario and is runnable from documented commands
- at least one contract benchmark shows behaviour that is not reducible to ordinary mean tick speed
- at least one generated or live-patched subtree experiment demonstrates why a Lisp runtime is useful beyond implementation taste
- the Nav2 adapter can run the wheeled scenario through a host capability boundary and emit canonical logs
- one ROS-backed row or slice exists in the evaluation outputs, preferably Nav2-backed; if not physical, it must at least be rosbag-backed and replay-validated
- two distinct scenarios exist with clear rationale, or the roadmap records why the second scenario was deferred to preserve the paper’s core claim
- all baseline and ROS evidence uses the same canonical event-log discipline as the core runtime experiments

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

## see also

- [Roadmap](limitations-roadmap.md)
- [todo](todo.md)
- [conformance levels](contracts/conformance.md)
- [runtime contract v1](contracts/runtime-contract-v1.md)
- [ros2 backend scope](integration/ros2-backend-scope.md)
- [cross-transport flagship for v0.5](integration/cross-transport-flagship.md)
