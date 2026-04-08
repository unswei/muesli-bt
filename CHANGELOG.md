# Changelog

muesli-bt

## [Unreleased]

This unreleased section captures the current `v0.5.0` baseline on `main`.

### Added
- Added a shared wheeled flagship BT path that is reused across Webots, PyBullet, and ROS2 with adapter-only wrapper differences.
- Added cross-transport normalisation and comparison tooling for the wheeled flagship, including scripted invariant checks that assert successful goal completion across the supported transport lanes.
- Added a stricter same-robot comparison lane with a PyBullet e-puck-style differential-drive surrogate, a checked-in strict comparison protocol, and scripted comparison coverage against the Webots e-puck path.
- Added an end-to-end ROS2 tutorial that walks the supported Ubuntu 22.04 + Humble path from build and attach through canonical log validation.
- Added an Isaac Sim TurtleBot3 ROS2 showcase with checked-in media and reproducible setup and capture instructions.
- Added an educational Webots goal-seeking BT entrypoint plus a transition guide that explains how the explicit teaching example leads into the shared flagship wrapper.

### Changed
- Refocused the public example and integration docs around the landed `v0.5` story: same BT, different IO transport, with a separate stricter same-robot lane.
- Polished the PyBullet e-puck demo surface with checked-in image and video assets, direct screenshot and MP4 export support, and a cluttered layout that still reaches the goal on the checked-in seed.
- Updated the README and example guides so the current flagship, ROS2 tutorial, Isaac showcase, and strict same-robot comparison path are easier to discover from the repository root.
- Expanded CI coverage so flagship comparison and same-robot comparison checks run from committed, self-contained fixtures instead of relying on local artefacts.

## [0.4.0] - 2026-03-21

### Added
- Added an optional benchmark harness under `bench/` with `A1`, `A2`, `B1`, `B2`, `B5`, and `B6`, CSV output, environment metadata capture, and progress reporting.
- Added benchmark result analysis and cross-runtime comparison scripts so `muesli-bt` and `BehaviorTree.CPP` runs can be summarised with the same output schema.
- Added evaluator tail-position tracking coverage for `if`, `begin`, `let`, `cond`, closure calls, and explicit deep-recursion cases through `and` / `or`.
- Added deep tail-recursion regression coverage for self recursion, mutual recursion, allocation pressure, and GC env-root stack behaviour.
- Added a first compiled-closure VM path that resolves supported special forms once, lowers params and `let` bindings to local slots, keeps globals/captures as named lookup, and executes supported tail calls through a bytecode-style `tail_call` path.
- Added regression coverage that distinguishes compiled closures from interpreter fallbacks, so supported closures compile and unsupported forms keep the existing evaluator path.
- Added interactive REPL coverage for command/history helpers so exit handling, `:clear`, and persistent-history path rules stay stable.
- Added a separate trace-level validator (`tools/validate_trace.py`) for `mbt.evt.v1` logs, covering cross-event checks such as `seq` ordering, completed tick delimitation, terminal `node_exit` uniqueness, deadline/cancellation evidence, async lifecycle ordering, deterministic comparison, batch reporting, and tolerant incomplete-tail handling.
- Added trace-validator smoke coverage plus example normalisation configs for deterministic replay and cross-backend comparison.

### Changed
- Optimised the reactive interruption path so the current `B2` benchmark cases no longer allocate on the steady-state hot path.
- Reduced benchmark full-trace logging overhead by switching the benchmark capture path to deferred event-size accounting instead of forced per-event serialisation.
- ROS-backed `env.run-loop` execution now emits direct canonical `mbt.evt.v1` logs through `event_log_path`, and the ROS `L2` replay/conformance artefacts now treat the canonical event log as the primary evidence stream.
- Replay tooling now treats simulator fixtures and ROS-backed artefacts through the same canonical log path, using `events.jsonl` per artefact directory and allowing `tools/validate_log.py` to accept either a direct log file or an artefact directory.
- `env.run-loop` canonical logs now emit `episode_begin`, `episode_end`, and `run_end` so long multi-episode experiments can be summarised from the canonical event stream directly.
- ROS canonical `run_start` capabilities now carry the explicit ROS time-source policy (`time_source`, `use_sim_time`, `obs_timestamp_source`) through the same log path used for simulator-backed runs.
- ROS `L2` replay/conformance checks and the Linux artefact verifier now expect the expanded canonical lifecycle (`episode_begin`, `episode_end`, `run_end`) instead of the older shorter event stream.
- `load` now resolves nested relative paths from the directory of the file that issued the nested `load`, so multi-file Lisp examples work reliably outside the repository root.
- Reorganised the Lisp evaluator into explicit internal dispatch/apply/sequence seams so tail-position handling is visible without changing the public evaluator API.
- Lisp tail calls now run through an internal trampoline loop instead of recurring through the host C++ stack on the covered evaluator paths.
- Tail-bounce GC polling now runs periodically during deep tail recursion instead of on every single bounce.
- Supported closure bodies now use a compiled execution path when they fit the first VM subset; unsupported closure bodies continue to run through the existing tree-walking evaluator.
- `muslisp` now uses a vendored `linenoise` REPL on interactive Linux and macOS terminals, with editable current lines, persistent history at `~/.muesli_bt_history`, wrapped input, and `:clear` for pending multi-line buffers.
- Validation docs now distinguish per-record schema validation from trace-level validation, and the main README, contracts README, and schema READMEs now point to both validator entry points.

### Fixed
- Fixed GC env-root handling so duplicate env roots behave like a stack and temporary evaluator roots no longer unregister long-lived roots accidentally.
- Fixed ROS `env.configure` option validation so backend-specific runs accept canonical logging options such as `event_log_path` and `event_log_ring_size`.
- Fixed the ROS `L2` C++ conformance helper to root decoded canonical event records correctly while parsing larger JSONL streams.

## [0.3.1] - 2026-03-14

### Added
- Added a separate GitHub release artefact for Ubuntu 22.04 + ROS 2 Humble that includes `muesli_bt::integration_ros2` and `muslisp_ros2`.
- Added ROS-enabled release-job validation for ROS contract tests, rosbag-backed `L2` replay verification, and ROS consumer smoke.

### Changed
- Release packaging now distinguishes the generic Ubuntu archive from the ROS-enabled Ubuntu 22.04 + Humble archive.
- Release docs now state explicitly that the generic Ubuntu archive is non-ROS and that the ROS-enabled archive requires a matching ROS 2 Humble runtime on the target host.

## [0.3.0] - 2026-03-14

### Added
- Added backend-specific `env.info` metadata support so optional integrations can expose schema ids, capability tags, and backend config without changing core BT semantics.
- Added stronger generic ROS2 skeleton coverage for configuration validation, reset policy, canonical observation/action shapes, and invalid-action fallback behaviour.
- Added Linux ROS-backed test harness coverage using `nav_msgs/msg/Odometry` input and `geometry_msgs/msg/Twist` output.
- Added installed-package ROS2 consumer smoke coverage on Ubuntu 22.04 + Humble.
- Added a Linux-only rosbag-backed ROS2 `L2` replay corpus and artefact output for nominal replay, clamped actions, invalid-action fallback, and reset-unsupported policy checks.
- Added push/PR CI gating for `conformance-l2-ros2-humble`, so the rosbag-backed ROS2 `L2` replay lane now runs in ordinary CI as well as scheduled or manual workflows.
- Added `tools/verify_ros2_l2_artifacts.py` so CI validates generated ROS2 `L2` summaries and run-loop artefacts structurally, not just by test exit status.
- Added ROS2 cleanup regression coverage for shutdown with a live transport peer.

### Changed
- ROS2 integration now requires real ROS package discovery when `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON`; configure fails cleanly when `rclcpp` is not discoverable.
- Core/no-ROS CI and release paths now disable `MUESLI_BT_BUILD_INTEGRATION_ROS2` explicitly so the portable baseline does not depend on host OS or ambient ROS setup.
- ROS2 backend now uses real Linux transport for `env.observe` / `env.act` / `env.step` via `Odometry` and `Twist`, while keeping canonical `ros2.obs.v1`, `ros2.state.v1`, and `ros2.action.v1`.
- ROS2 reset policy now defaults to `unsupported`; `reset_mode="stub"` is retained for deterministic harnesses and tests.
- ROS2 teardown now shuts `rclcpp` down before backend-registry reset, disables unused parameter services on the backend node, and allows `muslisp_ros2` to exit cleanly while live ROS peers are present.
- Package exports now preserve the correct installed share directory and ROS dependency discovery for downstream ROS2 consumers.
- ROS2 scope, backend-writing guidance, conformance notes, and backlog planning now reflect the first implemented Linux transport lane, the first replay corpus in `L2`, and the explicit first-milestone decision to keep real reset unsupported.

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
- Added authoritative event schema at `schemas/event_log/v1/mbt.evt.v1.schema.json` and schema guidance under `schemas/`.
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
