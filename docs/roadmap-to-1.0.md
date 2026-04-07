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

This means the remaining path to `v1.0.0` is not “add ROS2 somehow” or “make replay work somehow”.
The thin adaptor and observability baseline are already real enough that the next question is how to exploit them without widening the semantic surface.

The remaining path is:

- prove “same BT, different IO transport” on one flagship behaviour as paper-facing evidence
- define host capability bundles for richer external services without bloating `env.*` or `planner.plan`
- finish the async and cancellation correctness story for released runtime semantics
- keep the existing Isaac showcase as supporting evidence rather than a second semantic surface
- finish paper-facing evaluation and release hygiene

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

Further work should now move to cross-transport evidence, host-boundary definition, correctness hardening, or concrete consumer fixes rather than casually broadening the baseline transport.

#### `v0.5.0`: same BT, different IO transport

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
- expand planner conformance evidence under simulator and ROS-backed replay where it is cheap and reproducible
- keep planner time-budget behaviour explicit in docs and logs

Exit criteria:

- capability bundle registration and naming rules are documented without changing BT or Lisp semantics
- at least one manipulation contract and one perception contract are specified at the host level without hard-coding ROS library names into core semantics
- the split between `env.*`, `planner.plan`, and external host capabilities is explicit in docs and examples
- planner request/result docs match released behaviour and fixtures
- planner logs and canonical events cover the paper-critical success, timeout, and fallback paths
- at least one ROS-backed or rosbag-driven example demonstrates planning without introducing ROS-specific planner semantics

#### `v0.7.0`: async and cancellation correctness

Focus:

- finish the correctness story for cancellable async work

Scope:

- complete runtime-level async cancellation edge coverage
- add one ROS-level cancellation or pre-emption scenario only if it stays cheap and clean
- keep the main correctness argument in the runtime contract, not in ROS-specific glue

Exit criteria:

- async submit, poll, cancel, timeout, and late-completion-drop behaviour are all covered by deterministic tests or fixtures
- canonical logs show the required cancellation lifecycle events for the supported cases
- any ROS-level cancellation coverage reuses the same runtime semantics and does not introduce a second cancellation model

#### `v0.8.0`: flagship wheeled demo polish

Focus:

- make the paper-facing primary demo strong enough for both evaluation and presentation
- keep the supporting Isaac evidence current without turning it into a second primary track

Scope:

- Webots remains the visual flagship
- PyBullet remains the fast CI and development harness
- ROS-backed execution of the same wheeled behaviour becomes a deployability check
- keep the existing Isaac Sim / ROS2 H1 showcase as a modern-simulator evidence point
- treat Isaac as a ROS-integrated simulator host, not as a new semantic surface inside `muesli-bt`
- keep Isaac off the critical CI path unless it becomes cheap and reproducible enough to maintain

Exit criteria:

- docs, scripts, and assets support one clear flagship demo flow
- the wheeled demo has reproducible commands, expected outputs, and canonical log inspection steps
- one ROS-backed run of the flagship behaviour is documented and repeatable on the supported baseline
- the existing Isaac demo remains documented as a supporting ROS-backed deployability showcase
- the Isaac demo reuses existing BT semantics and canonical logging expectations rather than introducing simulator-specific semantics

#### `v0.9.0`: second scenario and evaluation hardening

Focus:

- reduce the risk that the paper story rests on one example only
- prove that the host capability bundle model works on a real non-transport ROS integration

Scope:

- add a second serious scenario or platform
- preferred candidate: Towers of Hanoi with a simulated robot arm
- treat MoveIt as the first concrete instance of a host capability bundle, specifically an arm-motion or goal-execution capability
- add a usable perception path for disc and peg state rather than relying on hidden oracle state in the paper-facing demo
- likely first concrete perception adapter, if it stays the lightest reproducible path: a YOLO-compatible detector feeding a scene-state normaliser rather than exposing raw detections directly to BT logic
- choose the simulator based on reproducibility, MoveIt compatibility, and usable perception first
- prefer Isaac Sim for the manipulator/Hanoi path if it gives the cleanest ROS + perception + manipulation story without breaking reproducibility
- use Webots + ROS only if it stays thin and repeatable enough; otherwise use the most reproducible MoveIt-friendly simulator
- if the earlier “booster” plan is still valid and stronger than Hanoi, compare both on evidence and scope, not novelty

Exit criteria:

- two distinct scenarios exist with clear rationale
- one non-wheeled scenario uses explicit host capability bundle boundaries for transport, manipulation, and perception
- preferred outcome: a simulated arm solves a bounded Towers of Hanoi task with MoveIt-backed motion and usable perception
- perception output is normalised into a stable scene-state layer before BT logic consumes it
- acceptable fallback: if Hanoi proves too heavy for the `1.0` paper path, replace it with a simpler manipulator benchmark and record why Hanoi was deferred
- evaluation scripts and result collection are reproducible from the tagged codebase
- at least one baseline comparison and one ROS-backed row or slice exist in the evaluation outputs

#### `v1.0.0`: paper artefacts and release baseline

Focus:

- cut the first paper-ready, tool-builder-friendly, release-quality baseline

Exit criteria:

- runtime contract, compatibility policy, and conformance docs match the tagged code exactly
- `L0`, `L1`, and `L2` are all documented with exact run commands and expected artefacts
- source release plus Ubuntu `x86_64` and macOS `arm64` release artefacts are published and verified
- at least one paper figure, table row, or evaluation slice includes ROS-backed evidence
- the core claim is demonstrably true: BT semantics stay stable while transport changes, and canonical event logs support replay and inspection across those transports

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

## gotchas

- Do not let ROS2 scope expand into a second semantic runtime.
- Do not choose a flagship that depends on backend-specific line sensors, wall sensors, or other signals that the released ROS2 baseline does not expose.
- Do not let “host capability bundles” turn into one giant catch-all ROS super-interface.
- Do not treat ad hoc replay artefacts as a replacement for canonical `mbt.evt.v1` logs.
- Do not use demo polish to hide missing conformance or observability guarantees.
- Do not lock the second flagship too early if the strongest reproducible evidence points elsewhere.

## see also

- [Roadmap](limitations-roadmap.md)
- [todo](todo.md)
- [conformance levels](contracts/conformance.md)
- [runtime contract v1](contracts/runtime-contract-v1.md)
- [ros2 backend scope](integration/ros2-backend-scope.md)
- [cross-transport flagship for v0.5](integration/cross-transport-flagship.md)
