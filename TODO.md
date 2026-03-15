# todo

This backlog is maintained forward from March 2, 2026.
It is aligned with `docs/roadmap-to-1.0.md`.

## now

- `v0.4.0`: prepare the release-candidate pass and tag once the current canonical ROS replay/logging docs and Linux validation results are committed

## next

- `v0.5.0`: choose one canonical wheeled BT and reuse it across PyBullet, Webots, and ROS2 with adapter-only differences
- `v0.5.0`: add scripted invariant checks that compare key behaviour or decision traces across simulator and ROS-backed runs
- `v0.5.0`: treat the cross-transport invariant checks as paper evidence, not just integration smoke
- `v0.5.0`: publish end-to-end ROS2 consumer docs/examples for the supported path
- `v0.5.0`: deepen ROS2 `L2` evidence only when there is a concrete new transport path or failure mode to cover
- `v0.5.0`: update GitHub Actions workflow dependencies off Node 20 before the June 2, 2026 GitHub cutoff

## later

- `v0.6.0`: define host capability bundles for external execution, navigation, and perception services without expanding `env.*` or `planner.plan`
- `v0.6.0`: define the first manipulator capability contract as a generic host bundle, with MoveIt as the intended first adapter rather than a core semantic surface
- `v0.6.0`: define the first perception capability contract as a generic host bundle, with any detector-specific adapter kept behind that contract
- `v0.6.0`: stabilise planner request/result semantics and make the boundary between `planner.plan` and external host capabilities explicit
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
