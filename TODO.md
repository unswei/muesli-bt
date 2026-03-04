# todo

This backlog is maintained forward from March 2, 2026.

## now

- prepare `v0.2.0` release artefacts (tag, release notes, and release workflow publish checks)
- run `muesli-studio` compatibility check against `v0.2.0` and record the result in `docs/contracts/studio-compatibility-matrix.md`

## next

- harden backend schema validation with explicit version compatibility checks
- extend observability exports for long-running multi-episode experiments
- add production-oriented backend adapters beyond simulator demos

## later

- complete ROS2 transport binding beyond the skeleton (`topics` / `actions` / `services`) and add backend lifecycle management with non-blocking executor spin
- complete ROS2 `env.api.v1` parity (`env.configure`, `env.reset`, `env.observe`, `env.act`, `env.step`) with explicit reset semantics and stable schema ids
- add ROS2 deadline/cancellation safety behaviour so missed budgets and invalid actions always resolve to a safe fallback path
- ensure ROS2 runtime effects are emitted through canonical `mbt.evt.v1` events only (no alternate external log format)
- add rosbag-driven ROS2 conformance fixtures plus backend-specific `L2` conformance tests and CI gating
- add Linux ROS2 CI lanes that build with `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON` and run ROS2 conformance checks
- publish end-to-end ROS2 consumer docs/examples (attach flow, configuration surface, troubleshooting and reproducibility notes)
