# todo

This backlog is maintained forward from March 2, 2026.
It is aligned with `docs/roadmap-to-1.0.md`.

## next

- `v0.8.0`: implement canonical host capability lifecycle events for real runtime-affecting capability calls; use `cap_call_start` and `cap_call_end` in `mbt.evt.v1`, including request id, capability name, operation, status, latency, rejection/error reason where applicable, and enough tick/job context for replay and first-divergence reports
- `v0.8.0`: implement the VLA backend integration plan for transport-transparent async host capabilities, with SmolVLA/LeRobot as the primary practical backend, OpenVLA-OFT as the heavyweight backend, replay-cache mode, validation, redaction, canonical lifecycle events, and evidence outputs
- `post-release`: revisit GitHub Pages deployment once `actions/configure-pages` or `actions/deploy-pages` ship a non-`node20` runtime upstream

## status notes

- released: supported runtime/API/example surfaces, plus items explicitly marked complete for `v0.7.0`
- experimental: implemented evidence paths that are not yet a stable external dependency surface, including Lisp DSL round-trip/hash evidence and generated-fragment rejection fixtures
- contract-only: documented host capability boundaries such as `cap.motion.v1` and `cap.perception.scene.v1`
- planned: roadmap work that is not released, including real `cap_call_start` / `cap_call_end` emission, production VLA providers, Nav2 adapters, MoveIt adapters, and generated subtree execution

## later

- `v0.8.0`: polish the flagship wheeled demo flow, assets, and reproducible run/log inspection steps
- `v0.8.0`: add one current Isaac Sim demo through ROS before `v1.0.0`, but keep it as a deployability/demo lane rather than a new semantic surface
- `v0.8.0`: finish the checked-in H1 Isaac demo with one Ubuntu 22.04 + Humble + NVIDIA smoke run, then freeze scope unless a concrete demo bug appears
- `v0.8.0`: keep the H1 Isaac demo as a bounded deployability lane and do not let it displace `v0.4.0` replay parity or `v0.5.0` cross-transport evidence
- `v0.8.0`: after the VLA backend path is implemented and covered by normal docs/evidence, delete `docs/integration/vla-backend-integration-plan.md` and replace temporary roadmap links with the final implementation pages
- `v0.9.0`: prefer a second serious scenario built around a simulated robot arm, with Towers of Hanoi as the preferred target if the simulator, MoveIt path, and perception stack stay reproducible
- `v0.9.0`: add a usable perception path for the manipulator scenario rather than relying on hidden oracle state in the evaluation demo
- `v0.9.0`: likely first concrete perception adapter, if it stays reproducible enough, is a YOLO-compatible detector feeding a scene-state normaliser rather than raw detections
- `v0.9.0`: choose the simulator on reproducibility, MoveIt compatibility, and perception quality first; prefer Isaac Sim if it gives the cleanest ROS-backed manipulator path without blowing up scope
- `v1.0.0`: finish evidence artefacts, baseline comparisons, release hygiene, and exact `L0` / `L1` / `L2` runbooks
- `post-1.0`: extend ROS2 transport binding beyond the first `Odometry` / `Twist` path (`topics` / `actions` / `services`)
- `post-1.0`: add a second supported Linux distro lane (Ubuntu 24.04 + Jazzy) after Humble is stable
- `post-1.0`: add production-oriented backend adapters beyond simulator demos and the thin ROS2 adaptor

## done

- `v0.5.0`: publish the final release notes from the green release baseline
- `v0.6.0`: define host capability bundle naming and registration rules without changing BT or Lisp semantics
- `v0.6.0`: make the boundary between `env.*`, `planner.plan`, and external host capabilities explicit in docs and examples
- `v0.6.0`: define the first motion/manipulation capability contract as a generic host bundle, with MoveIt as the intended first adapter rather than a core semantic surface
- `v0.6.0`: define the first perception capability contract as a generic host bundle, with any detector-specific adapter kept behind that contract
- `v0.6.0`: add one small BT-facing example that shows how host capability bundles are consumed without implying released runtime built-ins
- `v0.6.0`: review `cap.motion.v1` and `cap.perception.scene.v1` together for consistent request/result and status vocabulary
- `v0.6.0`: implement the smallest possible `cap.list` / `cap.describe` / `cap.call` registry path with a deterministic `cap.echo.v1` fixture
- `v0.6.0`: stabilise planner request/result semantics and make the boundary between `planner.plan` and external host capabilities explicit
- `v0.6.0`: audit ROS2 `L2` evidence and defer expansion until there is a concrete new ROS-backed planner, capability, transport, or failure-mode path to cover
- `v0.6.0`: add release notes and release checklist
- `v0.6.0`: confirm the tiny `cap.call` / `cap.echo.v1` registry path is enough for the release without canonical capability call events
- `v0.7.0`: confirm deterministic fixture and test coverage for async cancellation before start, cancellation while running, cancellation after timeout, repeated cancellation, and late completion after cancellation
- `v0.7.0`: add a thin ROS-level pre-emption fixture that reuses canonical `host_action_invalid` and `fallback_used` runtime events without introducing a ROS-specific cancellation model
- `v0.7.0`: collect the curated benchmark bundle and confirm the full `B8` group produced artefacts for all five async cancellation scenarios
- `v0.7.0`: decide that real host capability calls which can affect runtime behaviour must emit canonical capability lifecycle events; keep `cap.echo.v1` as a smoke path without extra events until a real adapter lands
- `v0.7.0`: add the `why Lisp as DSL?` page, representative DSL round-trip checks, source/canonical DSL hash logging for DSL-backed `bt_def` events, and deterministic negative generated-fragment fixtures
