# Changelog

This changelog starts on March 2, 2026 and tracks changes forward from this point.
Earlier development happened during rapid prototyping and was not recorded as a structured changelog.

## [Unreleased]

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
- Consumer smoketest now requires `muesli_bt::runtime` + `muesli_bt::integration_pybullet`.
- CI install consumer-smoketest now builds/install package with integration enabled and validates installed integration artefacts.
- Integration docs now define a stable C++ attach API path and explicit compatibility policy for inspector-facing API/schema changes.

### Fixed
- Eliminated docs/runtime drift where `episode_max` was documented but previously not executed as a true episode loop.
- `episode_max > 1` with reset-less backends now returns explicit `:unsupported` status with a clear message.
