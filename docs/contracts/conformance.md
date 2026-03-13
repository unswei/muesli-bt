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
  - runs on PR/push CI
  - this is where pre-Linux ROS2 contract work belongs: generic `env.*` semantics, schema decisions, and fixture intent
- `L1` simulator conformance:
  - PyBullet/Webots integration checks
  - additional push/PR CI gate for simulator-backed integration checks
- `L2` ROS 2 conformance:
  - rosbag-driven conformance checks
  - nightly or on-demand
  - Linux-only
  - true ROS2 transport, executor, distro, and packaging validation belongs here
  - backend scope and deliverables tracked in [ROS2 backend scope](../integration/ros2-backend-scope.md)
  - local Linux ROS-backed tests and install-smoke validation now exist; the remaining work is to wire them into automated `L2`

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

Run the explicit no-ROS portability path on Linux or macOS:

```bash
cmake --preset core-only
cmake --build --preset core-only -j
ctest --preset core-only --output-on-failure
```

This preset keeps `MUESLI_BT_BUILD_INTEGRATION_ROS2=OFF` explicitly, so reviewers can treat it as the supported cross-platform baseline.

Run the current Linux ROS2 validation locally on Ubuntu 22.04 + Humble:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/linux-ros2 -j
ctest --test-dir build/linux-ros2 --output-on-failure
```

Verify install/export plus the ROS2 consumer smoke:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-install-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/linux-install-root" \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/linux-install-ros2 -j
cmake --install build/linux-install-ros2
cmake -S tools/consumer_smoketest -B build/linux-consumer-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/build/linux-install-root"
cmake --build build/linux-consumer-ros2 -j
./build/linux-consumer-ros2/mbt_inspector_ros2_smoketest
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
- `fixtures/late-completion-drop-case`
- `fixtures/determinism-replay-case`

Published pre-Linux ROS2 surrogate bundles:

- `fixtures/ros2-observe-act-step-case`
- `fixtures/ros2-invalid-action-fallback-case`
- `fixtures/ros2-reset-unsupported-case`
- `fixtures/ros2-deadline-fallback-case`

## gotchas

- L0 should stay fast and hermetic; do not add simulator dependencies.
- Pre-Linux ROS2 work should extend L0-style generic coverage, not pretend to be transport conformance.
- The current Linux ROS2 validation uses a deterministic ROS-backed publisher/subscriber harness, not rosbag yet.
- L2 failures should signal integration risk and stay non-blocking by default.
- Deterministic fixtures are provenance-controlled; edit through update tooling only.

## see also

- [runtime contract v1](runtime-contract-v1.md)
- [compatibility policy](compatibility.md)
- [contracts index](README.md)
