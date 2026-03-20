# roadmap to 1.0

## what this is

This page turns the current backlog, ROS2 scope, conformance work, and paper-oriented release planning into one release roadmap from the current baseline to `v1.0.0`.

This page is release-oriented. The shorter [Roadmap](limitations-roadmap.md) page remains the theme-based backlog.

## when to use it

Use this page when you:

- decide whether a change belongs before or after `v1.0.0`
- scope release work for `v0.3.0+`
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

This means the remaining path to `v1.0.0` is not “add ROS2 somehow”.
The thin adaptor baseline is now real enough that the next question is how to exploit it without widening the semantic surface.

The remaining path is:

- close replay and observability gaps so ROS-backed runs are first-class contract artefacts
- prove “same BT, different IO transport” on one flagship behaviour as paper-facing evidence
- define host capability bundles for richer external services without bloating `env.*` or `planner.plan`
- finish paper-facing evaluation and release hygiene

### milestone plan

#### `v0.3.x`: ROS2 thin adaptor baseline landed

Focus:

- treat the thin ROS2 adaptor as a released baseline rather than a speculative milestone
- keep the supported transport small and explicit
- avoid reopening this scope except for concrete contract bugs or consumer-driven gaps

Scope:

- supported platform: Ubuntu 22.04 + ROS 2 Humble
- supported transport: `nav_msgs/msg/Odometry` in, `geometry_msgs/msg/Twist` out
- supported attach path: `(env.attach "ros2")`
- explicit reset policy: `unsupported` for real runs, `stub` only for deterministic harnesses and tests

Delivered in the `v0.3.x` release family:

- `v0.3.0` established the released thin-adaptor baseline, live ROS2 example path, rosbag-backed `L2`, and installed consumer smoke coverage
- `v0.3.1` added the dedicated ROS-enabled Ubuntu 22.04 + Humble release artefact path
- the first supported distro, message path, reset policy, and non-goals are now explicit in docs and release notes
- further work should move to observability parity, cross-transport evidence, or concrete consumer fixes rather than broadening the baseline transport casually

#### `v0.4.0`: replay and observability parity

Focus:

- make ROS-backed runs first-class citizens of the canonical observability story
- tighten replay verification from “artefact exists” to “log invariants are validated”
- strengthen the paper claim that simulator and ROS-backed runs share one inspection and replay surface

Scope:

- direct canonical `mbt.evt.v1` output for ROS-backed runs
- explicit time-source policy for ROS-backed runs (`sim time` vs wall time) and logging of that choice
- minimal replay verification CLI or equivalent command that checks invariants from a canonical event log
- tooling-facing parity so inspector and replay consumers can treat ROS-backed and simulator-backed runs the same way

Exit criteria:

- a ROS-backed run emits a validated canonical `mbt.evt.v1` log directly
- rosbag replay can be checked with one documented verification command
- replay verification covers at least event ordering, schema validity, and selected decision invariants
- docs explain which parts are deterministic, which parts are only bounded, and how to interpret replay failures
- a tooling consumer can inspect a ROS-backed run through the same canonical log path used for simulator-backed runs

Released in `v0.4.0`:

- ROS-backed runs now emit validated canonical `mbt.evt.v1` logs directly
- replay verification now has a documented trace-level validator path
- ROS-backed and simulator-backed runs now share the same canonical `events.jsonl` artefact shape for tooling consumers

#### `v0.5.0`: same BT, different IO transport

Focus:

- prove that muesli-bt semantics stay stable while the transport changes
- turn that result into one of the core paper evidence points, not just an integration smoke test

Scope:

- choose one canonical wheeled behaviour as the cross-transport flagship
- run the same high-level BT through PyBullet, Webots, and ROS2 with adapter-only differences
- document the common BT source and the transport-specific attach/config layer separately
- publish an improved integration tutorial that walks one supported backend path from attach/config through canonical log validation
- define a small set of scripted comparison checks for key behaviour, action, or decision-trace invariants across the three transports

Exit criteria:

- one flagship behaviour has a shared BT definition that is reused across simulator and ROS-backed runs
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
- add one current Isaac Sim demo before `v1.0.0` without changing core semantics

Scope:

- Webots remains the visual flagship
- PyBullet remains the fast CI and development harness
- ROS-backed execution of the same wheeled behaviour becomes a deployability check
- add one ROS-backed Isaac Sim demo as a modern-simulator evidence point before the paper release
- treat Isaac as a ROS-integrated simulator host, not as a new semantic surface inside `muesli-bt`
- keep Isaac off the critical CI path unless it becomes cheap and reproducible enough to maintain

Exit criteria:

- docs, scripts, and assets support one clear flagship demo flow
- the wheeled demo has reproducible commands, expected outputs, and canonical log inspection steps
- one ROS-backed run of the flagship behaviour is documented and repeatable on the supported baseline
- one current Isaac Sim demo exists and is documented as a pre-1.0 ROS-backed deployability demo
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

Example interpretation for `v0.3.0`:

If the ROS2 adaptor builds and the rosbag lane passes, but the run still does not emit direct canonical `mbt.evt.v1` output, the work is good enough for `v0.3.0` but not yet good enough for `v0.4.0`.

Example interpretation for `v1.0.0`:

If a ROS-backed demo works but the same BT cannot be shown across simulator and ROS transport, the deployability story is still incomplete.

## gotchas

- Do not let ROS2 scope expand into a second semantic runtime.
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
