# Changelog

This changelog starts on March 2, 2026 and tracks changes forward from this point.
Earlier development happened during rapid prototyping and was not recorded as a structured changelog.

## [Unreleased]

### Added
- Added root `TODO.md` as a forward-maintained backlog file.
- Added multi-episode `env.run-loop` tests for reset-capable and reset-less backends.

### Changed
- `env.run-loop` now supports real multi-episode execution when backend reset is available.
- `env.run-loop` now supports `step_max` as a per-episode step cap (`max_ticks` remains a compatibility key).
- `env.run-loop` result map now includes `episodes_completed`, `steps_total`, `last_episode_steps`, `last_status`, `ok`, and `error`.
- Integration/backend docs were updated to reflect actual backend implementation status and capabilities.
- `.instructions` env adapter spec now documents name-based `env.attach`.

### Fixed
- Eliminated docs/runtime drift where `episode_max` was documented but previously not executed as a true episode loop.
- `episode_max > 1` with reset-less backends now returns explicit `:unsupported` status with a clear message.
