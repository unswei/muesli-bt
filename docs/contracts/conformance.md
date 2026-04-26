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
    - runs in push/PR CI and remains available for scheduled or manual re-runs
    - Linux-only
    - true ROS2 transport, executor, distro, and packaging validation belongs here
    - backend scope and deliverables tracked in [ROS2 backend scope](../integration/ros2-backend-scope.md)
    - the current automated `L2` lane runs the ROS-backed contract tests plus a small rosbag-backed replay corpus
    - the current corpus covers nominal replay, clamped actions, invalid-action fallback, and explicit reset-unsupported policy artefacts
    - `v0.6.0` keeps this lane unchanged unless a concrete new ROS-backed planner, capability, transport, or failure-mode path needs coverage

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

ROS-backed `L2` artefacts follow the same canonical log naming:

- `ros2_l2_artifacts/<scenario>/events.jsonl`

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

For the live runner path, start the odometry publisher first, then run:

```bash
source /opt/ros/humble/setup.bash
./build/linux-ros2/muslisp_ros2 examples/repl_scripts/ros2-live-odom-twist.lisp
```

This can write a canonical event log such as `build/linux-ros2/ros2-live-run/events.jsonl` when the script configures `event_log_path`. The full topic-publisher recipe is documented in [ros2 backend scope](../integration/ros2-backend-scope.md).

Run the current rosbag-backed `L2` replay corpus locally:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2-l2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/linux-ros2-l2 --target muesli_bt_conformance_l2_rosbag_tests -j
ctest --test-dir build/linux-ros2-l2 -R muesli_bt_conformance_l2_rosbag_tests --output-on-failure
python3 tools/verify_ros2_l2_artifacts.py --artifact-root build/linux-ros2-l2/ros2_l2_artifacts
python3 tools/validate_log.py build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success
python3 tools/validate_trace.py check build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success
```

This verifier is the supported ROS-backed replay/conformance check for the current `L2` lane.
It validates canonical `mbt.evt.v1` output first, together with the scenario summaries produced by the rosbag suite.

For tooling consumers, the important part is that simulator-backed runs and ROS-backed runs now expose the same canonical artefact path:

- artefact directory
- `events.jsonl`

That means a validator or replay consumer can inspect both with the same entry point:

```bash
python3 tools/validate_log.py fixtures/determinism-replay-case
python3 tools/validate_trace.py check fixtures/determinism-replay-case
python3 tools/validate_log.py build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success
python3 tools/validate_trace.py compare \
  fixtures/determinism-replay-case/events.jsonl \
  fixtures/determinism-replay-case/events.jsonl \
  --profile deterministic
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
- `fixtures/async-cancel-before-start-case`
- `fixtures/async-cancel-while-running-case`
- `fixtures/async-cancel-after-timeout-case`
- `fixtures/async-repeated-cancel-case`
- `fixtures/async-late-completion-after-cancel-case`

Published pre-Linux ROS2 surrogate bundles:

- `fixtures/ros2-observe-act-step-case`
- `fixtures/ros2-invalid-action-fallback-case`
- `fixtures/ros2-reset-unsupported-case`
- `fixtures/ros2-deadline-fallback-case`

## gotchas

- L0 should stay fast and hermetic; do not add simulator dependencies.
- Pre-Linux ROS2 work should extend L0-style generic coverage, not pretend to be transport conformance.
- The current Linux ROS2 validation uses both a deterministic ROS-backed harness and a small rosbag-backed replay corpus.
- The first Linux milestone keeps real reset unsupported; `reset_mode="stub"` remains reserved for deterministic harnesses and tests.
- L2 failures now block ordinary CI for Linux ROS2 changes, so keep replay scenarios deterministic and narrowly scoped.
- Deterministic fixtures are provenance-controlled; edit through update tooling only.
- Deterministic replay claims apply to fixed-input fixture runs and deterministic harness lanes. Live ROS timing remains only bounded, not fully deterministic.
- When replay verification fails, check `seq` ordering and event invariants first. Treat timestamp-only drift as a timing/context issue unless decision payloads also diverge.

## see also

- [runtime contract v1](runtime-contract-v1.md)
- [compatibility policy](compatibility.md)
- [contracts index](README.md)
