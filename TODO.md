# todo

This backlog is maintained forward from March 2, 2026.

## now

- [x] finalise ROS2 backend scope documentation (`docs/integration/ros2-backend-scope.md`)
- [x] create an initial `integrations/ros2` skeleton
- [x] add documentation examples for `env.run-loop` observer callback patterns and multi-episode analytics
- [x] audit environment backend docs for strict key naming consistency (`obs_schema`, `action_schema`, `state_schema`)

### studio integration readiness

- [x] canonical contract published under `docs/contracts/`
- [x] canonical event schema published under `schema/`
- [x] deterministic fixture set committed under `tests/fixtures/mbt.evt.v1/`
- [x] CI schema validation and fixture drift gates enabled
- [x] CI install + consumer smoketest gate enabled
- [x] CI contract-change requires changelog acknowledgement
- [x] install exports include `muesli_bt_SHARE_DIR` and shared contract/schema assets
- [x] canonical event-line serialisation API available for inspector transport parity
- [x] deterministic host test mode API available (fixed seed + stable event ordering)
- [x] export integration target (`muesli_bt::integration_pybullet`) with installable headers
- [x] downstream consumer smoke proves runtime+integration attach and tick path via installed/public headers
- [x] optional Webots integration target export (`muesli_bt::integration_webots`) with installable headers and attach API
- [x] add extended fixture coverage for asynchronous cancellation edge cases
- [x] add dedicated Studio compatibility matrix against release tags and `main`

## next

- [x] reduce reliance on demo-specific extension built-ins where canonical `env.*` can cover the same path
- [x] add conformance tests for a second non-PyBullet backend through the generic `env.*` contract
- [x] expand CI checks for docs snippet freshness against `examples/**` source files

## later

- complete ROS2 transport binding beyond the skeleton (`topics` / `actions` / `services`) and add backend lifecycle management with non-blocking executor spin
- complete ROS2 `env.api.v1` parity (`env.configure`, `env.reset`, `env.observe`, `env.act`, `env.step`) with explicit reset semantics and stable schema ids
- add ROS2 deadline/cancellation safety behaviour so missed budgets and invalid actions always resolve to a safe fallback path
- ensure ROS2 runtime effects are emitted through canonical `mbt.evt.v1` events only (no alternate external log format)
- add rosbag-driven ROS2 conformance fixtures plus backend-specific `L2` conformance tests and CI gating
- add Linux ROS2 CI lanes that build with `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON` and run ROS2 conformance checks
- publish end-to-end ROS2 consumer docs/examples (attach flow, configuration surface, troubleshooting and reproducibility notes)
- harden backend schema validation with explicit version compatibility checks
- extend observability exports for long-running multi-episode experiments
- add production-oriented backend adapters beyond simulator demos
