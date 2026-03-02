# conformance levels

## what this is

This page defines the conformance matrix and how to run each level.

## when to use it

Use this page when:

- validating runtime-contract behaviour in CI
- reproducing paper fixtures
- integrating a new backend against the runtime contract

## how it works

### levels

- `L0` core-only conformance:
  - deterministic mock backend
  - no simulator dependency
  - runs on all PRs
- `L1` simulator conformance:
  - PyBullet/Webots integration checks
  - heavier runtime and environment checks
  - nightly or on-demand
- `L2` ROS 2 conformance:
  - rosbag-driven conformance checks
  - nightly or on-demand

### required artefacts

- tests:
  - `tests/conformance/mock_backend.hpp`
  - `tests/conformance/mock_backend.cpp`
  - `tests/conformance/test_conformance_main.cpp`
- fixtures:
  - `fixtures/<name>/config.json`
  - `fixtures/<name>/seed.json`
  - `fixtures/<name>/events.jsonl`
  - `fixtures/<name>/expected_metrics.json`
  - `fixtures/<name>/manifest.json`

## api / syntax

Run L0 locally:

```bash
cmake --preset dev
cmake --build --preset dev -j
ctest --preset dev -R muesli_bt_conformance_tests --output-on-failure
```

Validate fixture bundles:

```bash
python3 tools/fixtures/verify_fixture.py
```

Regenerate fixture bundles:

```bash
python3 tools/fixtures/update_fixture.py
```

## example

Published L0 fixture bundles:

- `fixtures/budget-warning-case`
- `fixtures/deadline-cancel-case`
- `fixtures/determinism-replay-case`

## gotchas

- L0 should stay fast and hermetic; do not add simulator dependencies.
- L1/L2 failures should signal integration risk but not block core authoring speed by default.
- Deterministic fixtures are provenance-controlled; edit through update tooling only.

## see also

- [runtime contract v1](runtime-contract-v1.md)
- [compatibility policy](compatibility.md)
- [contracts index](README.md)
