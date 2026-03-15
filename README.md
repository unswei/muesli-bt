# muesli-bt

[![Linux CI](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml)
[![Docs Pages](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml)
[![Docs](https://img.shields.io/badge/docs-online-brightgreen)](https://unswei.github.io/muesli-bt/)
[![Conformance](https://img.shields.io/badge/conformance-L0%2FL1%2FL2%20in%20CI-blue)](docs/contracts/conformance.md)

A compact Lisp runtime with an integrated Behaviour Tree engine, bounded-time planning, and async vision-language-action orchestration.

`muslisp` is the executable. `muesli-bt` is the runtime and project.

## Why muesli-bt

When control loops need to stay responsive, three things matter:

- decision logic that stays readable (`bt`/`defbt` in Lisp)
- compute-heavy reasoning that respects strict tick budgets (`plan-action` / `planner.plan`)
- asynchronous model calls that can stream and be cancelled (`vla.submit` / `vla.poll` / `vla.cancel`)

muesli-bt keeps those concerns in one place with explicit runtime semantics and built-in observability.

## Runtime Contract In One Minute

Guarantees:

1. Tick semantics are explicit and auditable (`tick_begin`/`tick_end`, ordered node events, stable tick index progression).
2. Budget and deadline handling is enforced at decision points with observable overrun/cancellation events.
3. Runtime observability uses one canonical external event stream: `mbt.evt.v1` JSONL with `contract_version`.

Host obligations:

1. The host owns time integration, sensor/actuator I/O, and safety fallback policy.
2. The host owns timeout policy and provides planner/VLA capability backends.

Authoritative contract artefacts:

- [runtime contract v1](docs/contracts/runtime-contract-v1.md)
- [`muesli-studio`](https://github.com/unswei/muesli-studio) integration contract: [docs/contracts/muesli-studio-integration.md](docs/contracts/muesli-studio-integration.md)
- [canonical event schema (`mbt.evt.v1`)](schemas/event_log/v1/mbt.evt.v1.schema.json)
- [deterministic fixtures](tests/fixtures/mbt.evt.v1/) and [fixture bundles](fixtures/)

## Conformance Levels

Conformance is layered so reviewers can separate core runtime semantics from heavier integration checks:

- `L0`: core-only runtime contract checks (fast, deterministic, PR-safe)
- `L1`: simulator integration conformance (PyBullet/Webots, push/PR CI gate)
- `L2`: ROS 2 conformance (rosbag-driven, push/PR CI gate on Ubuntu 22.04 + Humble)
- Generic `env.*` contract coverage in `muslisp_tests` includes both PyBullet and ROS2 backends.

Runbook and checklist: [conformance levels](docs/contracts/conformance.md).

## From Zero To Verified Demo

OS notes:

- macOS: `brew install cmake ninja python@3.11`
- Ubuntu/Debian: `sudo apt update && sudo apt install -y cmake ninja-build g++ python3 python3-venv`
- Core/no-ROS builds are supported on both Linux and macOS. ROS2 stays optional and Linux-only when enabled.
- For docs + `pybullet`, use `uv` to provision Python `3.11` into `.venv-py311` even on hosts whose system Python is older.

Build:

```bash
cmake --preset dev
cmake --build --preset dev -j
```

Optional benchmark harness:

```bash
cmake --preset bench-release
cmake --build --preset bench-release -j
./build/bench-release/bench/bench list
./build/bench-release/bench/bench run A1-single-leaf-off
```

The benchmark harness lives under [`bench/README.md`](bench/README.md) and writes CSV outputs into `bench/results/` by default.
Each benchmark session writes `run_summary.csv`, `aggregate_summary.csv`, and `environment_metadata.csv`.
Summarise the latest benchmark session with `python3 bench/scripts/analyse_results.py`.
Current benchmark coverage includes `A1`, `A2`, `B1`, `B2`, `B5`, and `B6`.
The current `B6` trace path uses deferred event-log serialisation when no file sink is enabled, while still reporting canonical event sizes.

Run the visual PyBullet racecar demo:

```bash
make demo-setup
make demo-run MODE=bt_planner
```

Verify install (single command; writes + validates canonical event log):

```bash
make verify-install
```

Additional runnable commands:

```bash
./build/dev/muslisp examples/bt/hello_bt.lisp
./build/dev/muslisp examples/repl_scripts/planner-bt-1d.lisp
./build/dev/muslisp examples/repl_scripts/a-star-grid.lisp
./build/dev/muslisp examples/repl_scripts/dijkstra-grid-pq.lisp
./build/dev/muslisp examples/repl_scripts/prm-2d-pq.lisp
./build/dev/muslisp examples/repl_scripts/vla-stub-bt.lisp
./build/dev/muslisp
```

On interactive Linux and macOS terminals, `muslisp` now uses a small vendored line editor for current-line editing, history, and wrapped multi-line input. Persistent history is stored at `~/.muesli_bt_history`, and `:clear` drops a pending multi-line buffer without leaving the REPL.

## ROS2 Release Baseline

The current `v0.3.x` ROS2 release baseline is intentionally narrow:

- supported host: Ubuntu 22.04 + ROS 2 Humble
- attach path: `(env.attach "ros2")`
- transport: `nav_msgs/msg/Odometry` in, `geometry_msgs/msg/Twist` out
- package export: `muesli_bt::integration_ros2`
- runner: `muslisp_ros2`
- reset policy: live runs use `reset_mode="unsupported"`; `stub` remains for tests and harnesses

Release artefact posture:

- `ubuntu-x86_64` archive: generic non-ROS build
- `ubuntu-22.04-ros2-humble-x86_64` archive: ROS-enabled build with `muesli_bt::integration_ros2` and `muslisp_ros2`
- the ROS-enabled archive requires a matching ROS 2 Humble runtime on the target host

Start with the [ROS2 tutorial](docs/integration/ros2-tutorial.md) for the boundary and live flow, then use [docs/integration/ros2-backend-scope.md](docs/integration/ros2-backend-scope.md) and [docs/contracts/conformance.md](docs/contracts/conformance.md) for the detailed commands and conformance lanes.

## muesli-studio integration

[`muesli-studio`](https://github.com/unswei/muesli-studio) is the inspector and tooling consumer for `muesli-bt` runtime data. This contract exists so integration behaviour stays stable and auditable across releases. The canonical runtime contract lives at [docs/contracts/runtime-contract-v1.md](docs/contracts/runtime-contract-v1.md), the Studio integration profile is [docs/contracts/muesli-studio-integration.md](docs/contracts/muesli-studio-integration.md), the authoritative event schema is [schemas/event_log/v1/mbt.evt.v1.schema.json](schemas/event_log/v1/mbt.evt.v1.schema.json), and deterministic fixtures are published under [tests/fixtures/mbt.evt.v1/](tests/fixtures/mbt.evt.v1/) and [fixtures/](fixtures/).

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(mbt_inspector ...)
target_link_libraries(mbt_inspector PRIVATE muesli_bt::runtime)
```

muesli_btConfig.cmake also defines `muesli_bt_SHARE_DIR`, which points to the installed contract and schema assets under `${prefix}/share/muesli_bt`.

If installed with `-DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=ON`, the package also exports `muesli_bt::integration_pybullet`:

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(mbt_inspector ...)
target_link_libraries(mbt_inspector PRIVATE muesli_bt::runtime muesli_bt::integration_pybullet)
```

If installed with `-DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=ON` and Webots SDK is available, the package exports `muesli_bt::integration_webots`:

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(mbt_inspector ...)
target_link_libraries(mbt_inspector PRIVATE muesli_bt::runtime muesli_bt::integration_webots)
```

The legacy `bt::integrations::webots::install_callbacks(...)` hook remains available as a compatibility shim, but new consumers do not need it.

If installed with `-DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON`, the package exports the first Linux ROS2 transport target `muesli_bt::integration_ros2`:

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(mbt_inspector ...)
target_link_libraries(mbt_inspector PRIVATE muesli_bt::runtime muesli_bt::integration_ros2)
```

Downstream consumers can probe optional integration targets safely:

```cmake
if(TARGET muesli_bt::integration_webots)
  target_link_libraries(mbt_inspector PRIVATE muesli_bt::integration_webots)
endif()

if(TARGET muesli_bt::integration_ros2)
  target_link_libraries(mbt_inspector PRIVATE muesli_bt::integration_ros2)
endif()
```

muesli-studio pins to tagged muesli-bt releases; a scheduled CI job may test against `main`.

## Unified Python Environment (Docs + PyBullet)

Use one Python version for docs tooling and `pybullet`: Python `3.11`.

Fast path:

```bash
./scripts/setup-python-env.sh
```

```bash
uv run python --version
uv venv --python 3.11 .venv-py311
uv pip install --python .venv-py311/bin/python -r docs/requirements.txt
CFLAGS='-Dfdopen=fdopen' CPPFLAGS='-Dfdopen=fdopen' \
  uv pip install --python .venv-py311/bin/python pybullet
```

Then use that same environment for docs:

```bash
.venv-py311/bin/python -m mkdocs serve
```

## Demo Make Targets

- `make demo-setup`: installs pinned demo deps and builds `muesli_bt_bridge`.
- `make demo-run MODE=bt_planner`: runs the racecar demo through the bridge/runtime path.
- `make verify-install`: runs a deterministic BT smoke run, writes `logs/verify-install.mbt.evt.v1.jsonl`, and validates it against `mbt.evt.v1`.

## Benchmark Harness

The benchmark harness is optional and separated from the main runtime tree under `bench/`.

Current first-milestone coverage:

- `A1` single-leaf baseline
- `A2` scheduler jitter trace
- `B1` static tick overhead
- `B2` reactive interruption
- `B5` parse, compile, load, and instantiate cost
- `B6` logging overhead

Use the `bench-release` preset for meaningful numbers, then run the generated `bench` executable from `build/bench-release/bench/`.

## Isaac Sim H1 Demo

For a Linux/NVIDIA showcase that keeps the current ROS2 thin adaptor intact, use the checked-in H1 locomotion demo:

- Lisp demo assets: `examples/isaac_h1_ros2_demo/`
- container/tooling helpers: `tools/docker/isaac_lab_vla_stack/`
- docs page: `docs/examples/isaac-h1-ros2-demo.md`

This path keeps `muesli-bt` on the released `Odometry` -> `Twist` ROS2 surface while Isaac Sim and NVIDIA's H1 controller own the low-level humanoid policy loop.

## VLA Integration Status

VLA support in this repository is currently **stub integration + contract hooks**.

What is real today:

- async interface surface (`vla.submit`, `vla.poll`, `vla.cancel`)
- cancellation lifecycle behaviour in BT nodes (`vla-request`, `vla-wait`, `vla-cancel`)
- canonical runtime logging for VLA lifecycle events (`mbt.evt.v1`)

What is placeholder today:

- production model transport/auth/provider integration (implemented by host backends, not by `muslisp` itself)

## A Small Hybrid Example

This pattern keeps ticking while a VLA job runs, uses planner output when available, and falls back safely:

```lisp
(defbt hybrid
  (sel
    (seq
      (cond goal-reached-1d state 1.0 0.05)
      (succeed))
    (seq
      (vla-wait :name "vision" :job_key vla-job :action_key action)
      (act apply-planned-1d state action state)
      (succeed))
    (seq
      (plan-action :name "planner"
                   :planner "mcts"
                   :state_key state
                   :action_key action
                   :budget_ms 10
                   :work_max 300)
      (act apply-planned-1d state action state)
      (succeed))
    (seq
      (vla-request :name "vision"
                   :job_key vla-job
                   :instruction_key instruction
                   :state_key state
                   :deadline_ms 25
                   :dims 1
                   :bound_lo -1.0
                   :bound_hi 1.0)
      (act bb-put-float action 0.0)
      (running))))
```

## What You Get

### Language runtime

- small Lisp core with closures and lexical scoping
- non-moving mark/sweep GC
- mutable `vec` and `map` containers
- deterministic RNG (`rng.make`, `rng.uniform`, `rng.normal`, `rng.int`)
- numeric helpers (`sqrt`, `log`, `exp`, `abs`, `clamp`)
- monotonic time (`time.now-ms`)
- JSON built-ins (`json.encode`, `json.decode`)

### Behaviour tree runtime

- composites:

    - memoryless: `seq`, `sel`
    - memoryful: `mem-seq`, `mem-sel`
    - yielding/reactive: `async-seq`, `reactive-seq`, `reactive-sel`

- decorators and leaves: `invert`, `repeat`, `retry`, `cond`, `act`, `succeed`, `fail`, `running`
- compile/load/save paths (`bt.compile`, `bt.to-dsl`, `bt.save`, `bt.load`, `bt.save-dsl`, `bt.load-dsl`)
- per-instance blackboard with typed values and write metadata
- scheduler-backed async actions and reactive pre-emption tracing (`node_preempt`, `node_halt`)

### Bounded-time planning

- `plan-action` node for strict per-tick budgeted planning
- `planner.plan` service for direct scripted experiments
- backend selection across MCTS, MPPI, and iLQR
- structured planner stats and logs

### VLA capability layer

- capability registry (`cap.list`, `cap.describe`)
- async job API (`vla.submit`, `vla.poll`, `vla.cancel`)
- stream-aware polling (`:queued`, `:running`, `:streaming`, `:done`, `:error`, `:timeout`, `:cancelled`)
- opaque media handles (`image_handle`, `blob_handle`) plus metadata accessors
- structured per-job logs (JSONL sink + in-memory dump)

### Environment capability layer

- canonical backend-agnostic control surface:

    - `env.info`, `env.attach`, `env.configure`
    - `env.reset`, `env.observe`, `env.act`, `env.step`
    - `env.run-loop`, `env.debug-draw`

- backends are registered through explicit integration extensions (no legacy backend-specific Lisp namespaces)

### Observability and control

- trace/log rings and dump APIs
- per-tree and per-node profiling stats
- scheduler stats and tick budget controls

## Documentation Map

- [Getting oriented](docs/getting-oriented/what-is-muesli-bt.md)
- [Terminology](docs/terminology.md)
- [Getting started](docs/getting-started.md)
- [Examples overview](docs/examples/index.md)
- [Example: A* search](docs/examples/a-star-search.md)
- [Tutorial: A* (step by step)](docs/examples/tutorials/a-star-step-by-step.md)
- [Example: Dijkstra with PQ](docs/examples/dijkstra-pq.md)
- [Tutorial: Dijkstra (step by step)](docs/examples/tutorials/dijkstra-step-by-step.md)
- [Example: PRM with PQ](docs/examples/prm-pq.md)
- [Tutorial: PRM (step by step)](docs/examples/tutorials/prm-step-by-step.md)
- [Language manual](docs/language/syntax.md)
- [Built-ins overview (`env.*` included)](docs/language/builtins.md)
- [Language reference index](docs/language/reference/index.md)
- [Behaviour trees](docs/bt/intro.md)
- [Planning overview](docs/planning/overview.md)
- [`planner.plan` request/result](docs/planning/planner-plan.md)
- [`plan-action` node integration](docs/planning/plan-action-node.md)
- [Integration overview](docs/integration/overview.md)
- [`env.*` integration API](docs/integration/env-api.md)
- [ROS2 tutorial](docs/integration/ros2-tutorial.md)
- [VLA integration](docs/bt/vla-integration.md)
- [VLA nodes reference](docs/bt/vla-nodes.md)
- [VLA request/response schema](docs/bt/vla-request-response.md)
- [Observability](docs/observability/logging.md)
- [Changelog](docs/changelog.md)
- [Release Notes](docs/releases/index.md)
- [TODO](docs/todo.md)
- [Roadmap to 1.0](docs/roadmap-to-1.0.md)
- [Roadmap](docs/limitations-roadmap.md)

Serve docs locally:

```bash
.venv-py311/bin/python -m mkdocs serve
```

## Tests

Run the suite:

```bash
ctest --test-dir build/dev --output-on-failure
```

## Repository Layout

- `src/`, `include/`: core runtime, evaluator, BT engine, planner, VLA integration
- `integrations/`: optional backend integrations (for example `integrations/pybullet/`, `integrations/webots/`, `integrations/ros2/`)
- `examples/bt/`: compact BT scripts
- `examples/repl_scripts/`: end-to-end experiments and demos
- `examples/pybullet_racecar/`: racecar demo package

    - `native/`: demo bridge entrypoint that uses canonical `env.api.v1` via the PyBullet integration

- `tests/`: unit/integration coverage
- `docs/`: user and internals documentation

## Contributing

Contributions are welcome.

If you add or change a feature, update:

- user docs in `docs/`
- feature reference pages under `docs/language/reference/`
- `docs/docs-coverage.md` to keep reference coverage complete
