# Changelog

This changelog starts on March 2, 2026 and tracks changes forward from this point.
Earlier development happened during rapid prototyping and was not recorded as a structured changelog.

## [Unreleased]

### Added
- Added backend-specific `env.info` metadata support so optional integrations can expose schema ids, capability tags, and backend config without changing core BT semantics.
- Added stronger generic ROS2 skeleton coverage for configuration validation, reset policy, canonical observation/action shapes, and invalid-action fallback behaviour.
- Added Linux ROS-backed test harness coverage using `nav_msgs/msg/Odometry` input and `geometry_msgs/msg/Twist` output.
- Added installed-package ROS2 consumer smoke coverage on Ubuntu 22.04 + Humble.
- Added a Linux-only rosbag-backed ROS2 `L2` replay test and artefact output for the first deterministic replay scenario.

### Changed
- ROS2 integration now requires real ROS package discovery when `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON`; configure fails cleanly when `rclcpp` is not discoverable.
- Core/no-ROS CI and release paths now disable `MUESLI_BT_BUILD_INTEGRATION_ROS2` explicitly so the portable baseline does not depend on host OS or ambient ROS setup.
- ROS2 backend now uses real Linux transport for `env.observe` / `env.act` / `env.step` via `Odometry` and `Twist`, while keeping canonical `ros2.obs.v1`, `ros2.state.v1`, and `ros2.action.v1`.
- ROS2 reset policy now defaults to `unsupported`; `reset_mode="stub"` is retained for deterministic harnesses and tests.
- Package exports now preserve the correct installed share directory and ROS dependency discovery for downstream ROS2 consumers.
- ROS2 scope, backend-writing guidance, conformance notes, and backlog planning now reflect the first implemented Linux transport lane and the first automated rosbag-backed `L2` replay case.

## [0.2.0] - 2026-03-04

### Added
- Added runtime contract v1 specification at `docs/contracts/runtime-contract-v1.md`.
- Added compatibility policy and release-note/changelog compatibility rules at `docs/contracts/compatibility.md`.
- Added conformance levels and execution guidance at `docs/contracts/conformance.md`.
- Added public runtime-contract headers under `include/muesli_bt/contract/`.
- Added L0 conformance test suite under `tests/conformance/` with deterministic mock backend.
- Added runtime-contract fixture bundle tooling:
  - `tools/fixtures/update_fixture.py`
  - `tools/fixtures/verify_fixture.py`
- Added published fixture bundles for:
  - budget warning case
  - deadline exceeded + cancellation case
  - determinism replay case
- Added docs snippet freshness checker at `scripts/check_docs_snippet_freshness.py`.
- Added CI docs snippet freshness gate in `.github/workflows/linux-ci.yml`.
- Added ROS2-on `env.*` backend contract regression coverage (`test_env_generic_ros2_backend_contract`) and CI lane (`ros2-linux-humble`).

### Changed
- Canonical event envelopes now include `contract_version`.
- `run_start` payload now includes runtime contract metadata (`contract_version`, `contract_id`).
- Event schema now tracks runtime-contract v1 events:
  - `node_enter`, `node_exit`
  - `budget_warning`, `deadline_exceeded`
  - `planner_call_start`, `planner_call_end`
  - `async_cancel_requested`, `async_cancel_acknowledged`, `async_completion_dropped`
- Runtime now emits budget/deadline decision-point events and planner start/end anchors.
- Runtime now tracks active async jobs per node and requests cancellation on tick deadline overrun.
- VLA cancel is idempotent at API level (`vla_service::cancel`).
- Canonical schema path is now published at `schemas/event_log/v1/mbt.evt.v1.schema.json` with validator `tools/validate_log.py`.
- Linux CI now includes:
  - explicit `conformance-l0` job on push/PR
  - nightly/on-demand `conformance-l1-sim` and `conformance-l2-ros2-humble` lanes
  - fixture-bundle drift and verification checks
- Linux CI now runs both `conformance-l0` and `conformance-l1-sim` on push/PR/workflow-dispatch; `conformance-l2-ros2-humble` remains schedule/on-demand.
- README and conformance docs now reflect `L1` as the simulator-backed CI conformance lane while retaining `L0` core coverage.
- Webots callback attach flow no longer requires explicit callback installation for new consumers; `bt::integrations::webots::install_callbacks(...)` remains as a compatibility shim.
- Core demo callback registration now includes `bb-truthy` and `select-action`.

## [0.1.0] - 2026-03-02

### Added
- Added root `TODO.md` as a forward-maintained backlog file.
- Added multi-episode `env.run-loop` tests for reset-capable and reset-less backends.
- Added canonical Studio integration contract at `docs/contracts/muesli-studio-integration.md`.
- Added contracts index at `docs/contracts/README.md`.
- Added authoritative event schema at `schema/mbt.evt.v1.schema.json` and schema guidance at `schema/README.md`.
- Added deterministic event fixtures under `tests/fixtures/mbt.evt.v1/`.
- Added event log validator tool at `tools/validate_event_log.py`.
- Added deterministic fixture generator at `tools/gen_fixtures_event_log.cpp`.
- Added install consumer smoketest project at `tools/consumer_smoketest/`.
- Added canonical event-line serialisation API in `bt::event_log::serialise_event_line(...)`.
- Added event line listener API in `bt::event_log::set_line_listener(...)` for pre-serialised stream transport.
- Added runtime deterministic test mode helper in `bt::runtime_host::enable_deterministic_test_mode(...)`.
- Added unit tests covering deterministic event mode and canonical serialisation parity.
- Added installed/public PyBullet integration headers (`pybullet/extension.hpp`, `pybullet/racecar_demo.hpp`).
- Added downstream smoketest flow that links runtime + integration target and exercises adapter attach + host tick path.
- Added Webots integration library sources under `integrations/webots/` with installable public attach API (`webots/extension.hpp`).
- Added optional Webots consumer smoketest compile target that validates public attach API and link contract when exported.
- Added contributor workflow guide at `CONTRIBUTING.md` (build, test, docs, format, boundary checks).
- Added `core-only` CMake preset for deterministic runtime boundary verification.
- Added package-consumption docs page at `docs/getting-started-consume.md`.
- Added flagship demo visual assets under `docs/assets/demos/`.
- Added deterministic fixture coverage for scheduler cancellation, VLA cancellation, deadline fallback, and reset-less unsupported loop semantics.
- Added CI jobs for Linux docs strict mode, Linux core sanitizers (ASan/UBSan), and macOS core build/test.
- Added release workflow (`.github/workflows/release.yml`) that builds and publishes:
  - Ubuntu x86_64 binary archive
  - macOS arm64 (Apple Silicon) binary archive
  - GitHub Release assets with SHA256 checksums

### Changed
- `env.run-loop` now supports real multi-episode execution when backend reset is available.
- `env.run-loop` now supports `step_max` as a per-episode step cap (`max_ticks` remains a compatibility key).
- `env.run-loop` result map now includes `episodes_completed`, `steps_total`, `last_episode_steps`, `last_status`, `ok`, and `error`.
- Integration/backend docs were updated to reflect actual backend implementation status and capabilities.
- Environment adapter specification now documents name-based `env.attach`.
- Root README now includes a dedicated `muesli-studio integration` section with contract/schema/fixture links and package consumer snippet.
- Linux CI now enforces schema validation, fixture drift checks, installed-package consumer smoketest, and contract-change changelog acknowledgement.
- CMake install/export now publishes `muesli_btConfig.cmake` and `muesli_bt::runtime` for external consumers.
- CMake install now publishes inspector share assets under `${prefix}/share/muesli_bt` (contract + schema).
- `muesli_btConfig.cmake` now exports `muesli_bt_SHARE_DIR` for tooling to resolve installed shared assets.
- CI install smoketest now verifies installed share assets and `muesli_bt_SHARE_DIR` export.
- CMake package export now publishes integration target `muesli_bt::integration_pybullet` when built with `MUESLI_BT_BUILD_INTEGRATION_PYBULLET=ON`.
- CMake package export now publishes integration target `muesli_bt::integration_webots` when built with `MUESLI_BT_BUILD_INTEGRATION_WEBOTS=ON` and Webots SDK is available.
- Consumer smoketest now supports `muesli_bt::runtime` plus whichever optional integration target is exported (`muesli_bt::integration_pybullet` or `muesli_bt::integration_webots`).
- CI install consumer-smoketest now builds/install package with integration enabled and validates installed integration artefacts.
- Integration docs now define a stable C++ attach API path and explicit compatibility policy for inspector-facing API/schema changes.
- CMake now detects missing Webots SDK and omits `integration_webots` cleanly (core/runtime still build and install).
- Webots integration export now injects macOS runtime rpath to Webots root (not `Contents/lib/controller`) so downstream dyld lookup resolves `@rpath/Contents/lib/controller/*.dylib` correctly.
- CI now includes a macOS Webots consumer smoketest that installs Webots, links `muesli_bt::integration_webots`, and runs the consumer binary to catch runtime linker regressions.
- Linux CI now runs on `release/*` branch pushes so release branch gates stay visible.
- Docs Pages deploy now runs only on push to `main`, `master`, or `legacy` so manual non-default-branch verification runs do not fail at deploy time.
- Updated `INSTALL.txt` to document the supported `cmake --install` and `find_package(muesli_bt CONFIG REQUIRED)` flow.
- Updated docs home page with Studio/tool-builder callouts and flagship demo thumbnails.
- Updated flagship demo pages with run recipes and behaviour-check guidance.
- Expanded contracts index with a compatibility matrix.
- Expanded testing docs to list the canonical fixture suite and high-risk behaviour fixtures.

### Fixed
- Eliminated docs/runtime drift where `episode_max` was documented but previously not executed as a true episode loop.
- `episode_max > 1` with reset-less backends now returns explicit `:unsupported` status with a clear message.
