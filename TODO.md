# todo

This backlog is maintained forward from March 2, 2026.

## now

- prepare `v0.2.0` release artefacts (tag, release notes, and release workflow publish checks)
- run `muesli-studio` compatibility check against `v0.2.0` and record the result in `docs/contracts/studio-compatibility-matrix.md`
- document the first supported Linux ROS2 lane clearly: Ubuntu 22.04, Humble, `Odometry` in, `Twist` out, and explicit reset policy
- wire the current Linux ROS-backed tests and install/export smoke into an automated `L2` CI lane
- record the exact Linux bootstrap steps and troubleshooting notes for sourced ROS environments and CMake package discovery

## next

- harden backend schema validation with explicit version compatibility checks
- extend observability exports for long-running multi-episode experiments
- add production-oriented backend adapters beyond simulator demos
- add rosbag-backed deterministic evidence on top of the current ROS-backed publisher/subscriber harness
- add explicit canonical event-log assertions for Linux ROS-backed runs, not just result-map checks

## later

- extend ROS2 transport binding beyond the first `Odometry` / `Twist` path (`topics` / `actions` / `services`)
- add ROS2 deadline/cancellation safety behaviour so missed budgets and invalid actions always resolve to a safe fallback path
- ensure ROS2 runtime effects are emitted through canonical `mbt.evt.v1` events only (no alternate external log format)
- add rosbag-driven ROS2 conformance fixtures plus backend-specific `L2` conformance tests and CI gating
- add a second supported Linux distro lane (Ubuntu 24.04 + Jazzy) only after Humble is stable
- publish end-to-end ROS2 consumer docs/examples (attach flow, configuration surface, troubleshooting and reproducibility notes)
