# todo

This backlog is maintained forward from March 2, 2026.
It is aligned with `docs/roadmap-to-1.0.md`.

## next

- `v0.6.0`: refine host capability bundles for external execution, navigation, manipulation, and perception services without expanding `env.*` or `planner.plan`
- `v0.6.0`: decide whether the tiny `cap.call` / `cap.echo.v1` registry path is enough for the release, or whether it needs canonical capability call events
- `v0.6.0`: revisit GitHub Pages deployment once `actions/configure-pages` or `actions/deploy-pages` ship a non-`node20` runtime upstream

## later

- `v0.7.0`: add extended fixture coverage for async cancellation edge cases and optional ROS-level pre-emption coverage only if it stays thin
- `v0.8.0`: polish the flagship wheeled demo flow, assets, and reproducible run/log inspection steps
- `v0.8.0`: add one current Isaac Sim demo through ROS before `v1.0.0`, but keep it as a deployability/demo lane rather than a new semantic surface
- `v0.8.0`: finish the checked-in H1 Isaac demo with one Ubuntu 22.04 + Humble + NVIDIA smoke run, then freeze scope unless a concrete demo bug appears
- `v0.8.0`: keep the H1 Isaac demo as a bounded deployability lane and do not let it displace `v0.4.0` replay parity or `v0.5.0` cross-transport evidence
- `v0.9.0`: prefer a second serious scenario built around a simulated robot arm, with Towers of Hanoi as the preferred target if the simulator, MoveIt path, and perception stack stay reproducible
- `v0.9.0`: add a usable perception path for the manipulator scenario rather than relying on hidden oracle state in the paper-facing demo
- `v0.9.0`: likely first concrete perception adapter, if it stays reproducible enough, is a YOLO-compatible detector feeding a scene-state normaliser rather than raw detections
- `v0.9.0`: choose the simulator on reproducibility, MoveIt compatibility, and perception quality first; prefer Isaac Sim if it gives the cleanest ROS-backed manipulator path without blowing up scope
- `v1.0.0`: finish paper artefacts, baseline comparisons, release hygiene, and exact `L0` / `L1` / `L2` runbooks
- `post-1.0`: extend ROS2 transport binding beyond the first `Odometry` / `Twist` path (`topics` / `actions` / `services`)
- `post-1.0`: add a second supported Linux distro lane (Ubuntu 24.04 + Jazzy) after Humble is stable
- `post-1.0`: add production-oriented backend adapters beyond simulator demos and the thin ROS2 adaptor

## done

- `v0.5.0`: cut the release from the current green `main` baseline and publish the final release notes
- `v0.6.0`: define host capability bundle naming and registration rules without changing BT or Lisp semantics
- `v0.6.0`: make the boundary between `env.*`, `planner.plan`, and external host capabilities explicit in docs and examples
- `v0.6.0`: define the first motion/manipulation capability contract as a generic host bundle, with MoveIt as the intended first adapter rather than a core semantic surface
- `v0.6.0`: define the first perception capability contract as a generic host bundle, with any detector-specific adapter kept behind that contract
- `v0.6.0`: add one small BT-facing example that shows how host capability bundles are consumed without implying released runtime built-ins
- `v0.6.0`: review `cap.motion.v1` and `cap.perception.scene.v1` together for consistent request/result and status vocabulary
- `v0.6.0`: implement the smallest possible `cap.list` / `cap.describe` / `cap.call` registry path with a deterministic `cap.echo.v1` fixture
- `v0.6.0`: stabilise planner request/result semantics and make the boundary between `planner.plan` and external host capabilities explicit
- `v0.6.0`: audit ROS2 `L2` evidence and defer expansion until there is a concrete new ROS-backed planner, capability, transport, or failure-mode path to cover
