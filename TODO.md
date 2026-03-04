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

- reduce reliance on demo-specific extension built-ins where canonical `env.*` can cover the same path
- add conformance tests for a second non-PyBullet backend through the generic `env.*` contract
- expand CI checks for docs snippet freshness against `examples/**` source files

## later

- add production-oriented backend adapters beyond simulator demos
- harden backend schema validation with explicit version compatibility checks
- extend observability exports for long-running multi-episode experiments
