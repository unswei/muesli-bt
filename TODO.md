# todo

This backlog is maintained forward from March 2, 2026.

## now

- prepare `v0.2.0` release artefacts (tag, release notes, and release workflow publish checks)
- run `muesli-studio` compatibility check against `v0.2.0` and record the result in `docs/contracts/studio-compatibility-matrix.md`

## next

- harden backend schema validation with explicit version compatibility checks
- extend observability exports for long-running multi-episode experiments
- add production-oriented backend adapters beyond simulator demos
- deepen ROS2 `L2` evidence beyond the current replay corpus only when there is a concrete new transport path or failure mode to cover
- add explicit canonical event-log assertions for Linux ROS-backed runs once those runs emit canonical `mbt.evt.v1` output directly rather than run-loop artefact JSONL

## later

- extend ROS2 transport binding beyond the first `Odometry` / `Twist` path (`topics` / `actions` / `services`)
- ensure ROS2 runtime effects are emitted through canonical `mbt.evt.v1` events only (no alternate external log format)
- add a second supported Linux distro lane (Ubuntu 24.04 + Jazzy) only after Humble is stable
- publish end-to-end ROS2 consumer docs/examples (attach flow, configuration surface, troubleshooting and reproducibility notes)
