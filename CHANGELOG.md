# Changelog

This changelog starts on March 2, 2026 and tracks changes forward from this point.
Earlier development happened during rapid prototyping and was not recorded as a structured changelog.

## [Unreleased]

No unreleased entries.

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
- Consumer smoketest now requires `muesli_bt::runtime` + `muesli_bt::integration_pybullet`.
- CI install consumer-smoketest now builds/install package with integration enabled and validates installed integration artefacts.
- Integration docs now define a stable C++ attach API path and explicit compatibility policy for inspector-facing API/schema changes.
- CMake now detects missing Webots SDK and omits `integration_webots` cleanly (core/runtime still build and install).
- Webots integration export now injects macOS runtime rpath to Webots root (not `Contents/lib/controller`) so downstream dyld lookup resolves `@rpath/Contents/lib/controller/*.dylib` correctly.
- CI now includes a macOS Webots consumer smoketest that installs Webots, links `muesli_bt::integration_webots`, and runs the consumer binary to catch runtime linker regressions.
- Updated `INSTALL.txt` to document the supported `cmake --install` and `find_package(muesli_bt CONFIG REQUIRED)` flow.
- Updated docs home page with Studio/tool-builder callouts and flagship demo thumbnails.
- Updated flagship demo pages with run recipes and behaviour-check guidance.
- Expanded contracts index with a compatibility matrix.
- Expanded testing docs to list the canonical fixture suite and high-risk behaviour fixtures.

### Fixed
- Eliminated docs/runtime drift where `episode_max` was documented but previously not executed as a true episode loop.
- `episode_max > 1` with reset-less backends now returns explicit `:unsupported` status with a clear message.
