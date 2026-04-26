# muesli-bt

[![Linux CI](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml)
[![Docs Pages](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml)
[![Docs](https://img.shields.io/badge/docs-online-brightgreen)](https://unswei.github.io/muesli-bt/)
[![Conformance](https://img.shields.io/badge/conformance-L0%2FL1%2FL2%20in%20CI-blue)](docs/contracts/conformance.md)

A compact Lisp runtime with an integrated Behaviour Tree engine, bounded-time planning, and async vision-language-action orchestration.

`muslisp` is the executable. `muesli-bt` is the runtime and project.

If you are new to the repository, start with [Getting Started](docs/getting-started.md) and [Examples Overview](docs/examples/index.md).

## Why muesli-bt

When control loops need to stay responsive, three things matter:

- decision logic that stays readable (`bt`/`defbt` in Lisp)
- compute-heavy reasoning that respects strict tick budgets (`plan-action` / `planner.plan`)
- asynchronous model calls that can stream and be cancelled (`vla.submit` / `vla.poll` / `vla.cancel`)

muesli-bt keeps those concerns in one place with explicit runtime semantics and built-in observability.

### Benchmarks

The optional benchmark harness lives under [`bench/`](bench/README.md). It covers runtime latency, lifecycle costs, logging overhead, strict allocation checks, GC/memory evidence, async cancellation contract edges, and the optional shared-subset comparison against BehaviorTree.CPP 4.9.0.

Current benchmark interpretation lives in [runtime performance](docs/internals/runtime-performance.md). Keep detailed benchmark claims there or in release artefacts rather than scattering result snapshots through this README.

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

Validation tools:

- schema validation: [`tools/validate_log.py`](tools/validate_log.py)
- trace-level validation and replay comparison: [`tools/validate_trace.py`](tools/validate_trace.py)

## Conformance Levels

Conformance is layered so reviewers can separate core runtime semantics from heavier integration checks:

- `L0`: core-only runtime contract checks (fast, deterministic, PR-safe)
- `L1`: simulator integration conformance (PyBullet/Webots, push/PR CI gate)
- `L2`: ROS 2 conformance (rosbag-driven, push/PR CI gate on Ubuntu 22.04 + Humble)
- Generic `env.*` contract coverage in `muslisp_tests` includes both PyBullet and ROS2 backends.

Runbook and checklist: [conformance levels](docs/contracts/conformance.md).
Validation overview: [contracts index](docs/contracts/README.md#validations).

## From Zero To Verified Demo

OS notes:

- macOS: `brew install cmake ninja python@3.11`
- Ubuntu/Debian: `sudo apt update && sudo apt install -y cmake ninja-build g++ python3 python3-venv`
- Core/no-ROS builds are supported on both Linux and macOS. ROS2 stays optional and Linux-only when enabled.
- For docs + `pybullet`, use `uv` with Python `3.11`.

Build:

```bash
cmake --preset dev
cmake --build --preset dev -j
```

Optional benchmark harness: see [`bench/README.md`](bench/README.md) for build, run and comparison instructions.

Run the visual PyBullet racecar demo:

```bash
make demo-setup
make demo-run MODE=bt_planner
```

Run the PyBullet e-puck-style differential-drive flagship demo:

```bash
make demo-setup
PYTHONPATH=build/dev/python \
  .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --headless
```

Guide: [PyBullet: e-puck-style goal seeking](docs/examples/pybullet-epuck-goal.md)

Shared flagship guides:

- [cross-transport flagship comparison](docs/examples/cross-transport-flagship-comparison.md)
- [ROS2 tutorial](docs/integration/ros2-tutorial.md)
- [same-robot strict comparison](docs/integration/same-robot-strict-comparison.md)

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

## v0.6 Release Baseline

The current `v0.6.x` baseline keeps the `v0.5.0` cross-transport flagship intact and adds the first host capability boundary contracts for future higher-level services.

The flagship transport surface remains intentionally narrow and uses one shared wheeled BT across the main backend paths:

- shared flagship BT reused across Webots, PyBullet, and ROS2
- strict host boundary: backend wrappers own observation shaping and command mapping
- supported ROS host: Ubuntu 22.04 + ROS 2 Humble
- supported ROS transport: `nav_msgs/msg/Odometry` in, `geometry_msgs/msg/Twist` out
- scripted cross-transport comparison and same-robot strict comparison checks
- canonical logging through `mbt.evt.v1`

The new v0.6 capability boundary is deliberately small:

- `cap.list`, `cap.describe`, and `cap.call` are available as the first registry/API path
- `cap.echo.v1` is the only implemented fixture capability
- `cap.motion.v1` and `cap.perception.scene.v1` are documented host capability contracts, not released robot adapters
- MoveIt, Nav2, detector, and perception adapters are host-side future work behind those contracts

The ROS2 part of that baseline remains intentionally narrow:

- supported host: Ubuntu 22.04 + ROS 2 Humble
- attach path: `(env.attach "ros2")`
- transport: `nav_msgs/msg/Odometry` in, `geometry_msgs/msg/Twist` out
- package export: `muesli_bt::integration_ros2`
- runner: `muslisp_ros2`
- reset policy: live runs use `reset_mode="unsupported"`; `stub` remains for tests and harnesses
- canonical logging path: `event_log_path` writes direct `mbt.evt.v1` JSONL to `events.jsonl`-style artefact paths
- canonical lifecycle summary: long runs emit `episode_begin`, `episode_end`, and `run_end`
- ROS timing metadata: `run_start.data.capabilities` records `time_source`, `use_sim_time`, and `obs_timestamp_source`

Release artefact posture:

- `ubuntu-x86_64` archive: generic non-ROS build
- `ubuntu-22.04-ros2-humble-x86_64` archive: ROS-enabled build with `muesli_bt::integration_ros2` and `muslisp_ros2`
- the ROS-enabled archive requires a matching ROS 2 Humble runtime on the target host

Start with the [ROS2 tutorial](docs/integration/ros2-tutorial.md) for the supported end-to-end build, run, and canonical log validation flow, then use [docs/integration/ros2-backend-scope.md](docs/integration/ros2-backend-scope.md) and [docs/contracts/conformance.md](docs/contracts/conformance.md) for the backend boundary and conformance lanes.

For the flagship comparison story itself, use:

- [cross-transport flagship comparison](docs/examples/cross-transport-flagship-comparison.md)
- [same-robot strict comparison](docs/integration/same-robot-strict-comparison.md)
- [PyBullet: e-puck-style goal seeking](docs/examples/pybullet-epuck-goal.md)
- [Isaac Sim: TurtleBot3 ROS2 demo](docs/examples/isaac-wheeled-ros2-showcase.md)

## v0.7 Development Evidence

The active `v0.7.0` work is focused on runtime defensibility, async correctness, and benchmark evidence. The codebase now has the main evidence lanes in place:

- `tick_audit` can emit per-tick allocation, heap, GC-delta, node-path, and logging-mode evidence through the canonical event stream.
- GC lifecycle events and GC policy switches are exposed through the runtime and Lisp builtins.
- strict precompiled-tick allocation tests fail unexpected allocations outside explicit logging whitelist scopes.
- deterministic async fixtures and `B8` benchmarks cover cancel before start, cancel while running, cancel after timeout, repeated cancel, and late completion after cancellation.
- `B7` benchmark scenarios keep canonical GC/memory evidence and summarise GC pause, heap-live slope, RSS slope, and event-log bytes per tick in the CSV schema.
- each benchmark result directory now includes `experiment_manifest.json` so environment, build, runtime flags, seed, scenario, and trace schema metadata travel with the CSV summaries.
- trace comparison reports precise first-divergence context, including tick, event type, field path, node id, blackboard key, async job id, planner id, or host capability where those fields exist.

The roadmap tracks remaining release work in [Roadmap to 1.0](docs/roadmap-to-1.0.md). The benchmark command reference lives in [bench/README.md](bench/README.md).

## package and tooling integration

`muesli-bt` installs a CMake package for runtime consumers and tooling such as [`muesli-studio`](https://github.com/unswei/muesli-studio). Start with the [runtime contract](docs/contracts/runtime-contract-v1.md), the [tooling integration profile](docs/contracts/muesli-studio-integration.md), and the [package consumption guide](docs/getting-started-consume.md).

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(mbt_inspector ...)
target_link_libraries(mbt_inspector PRIVATE muesli_bt::runtime)
```

muesli_btConfig.cmake also defines `muesli_bt_SHARE_DIR`, which points to the installed contract and schema assets under `${prefix}/share/muesli_bt`.

Optional integration targets may also be exported when enabled at build time:

- `muesli_bt::integration_pybullet`
- `muesli_bt::integration_webots`
- `muesli_bt::integration_ros2`

The detailed target matrix, contract assets, and consumer guidance live in:

- [docs/getting-started-consume.md](docs/getting-started-consume.md)
- [docs/contracts/README.md](docs/contracts/README.md)
- [docs/contracts/muesli-studio-integration.md](docs/contracts/muesli-studio-integration.md)

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
- `PYTHONPATH=build/dev/python .venv-py311/bin/python examples/pybullet_epuck_goal/run_demo.py --headless`: runs the e-puck-style PyBullet flagship path.
  Guide: [`docs/examples/pybullet-epuck-goal.md`](docs/examples/pybullet-epuck-goal.md)
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
- `B7` GC and memory evidence smoke runs
- `B8` async cancellation contract edge smoke runs

Use the `bench-release` preset for meaningful numbers, then run the generated `bench` executable from `build/bench-release/bench/`. `run-all` is the reasonable whole-catalogue runner. Use `bench/scripts/run_publication_benchmarks.py` for publication-quality bundles with stronger durations, repetitions, manifests, and generated figure/report artefacts.

## Isaac Sim H1 Demo

For a Linux/NVIDIA humanoid example that keeps the current ROS2 thin adaptor intact, use the checked-in H1 locomotion demo:

- Lisp demo assets: `examples/isaac_h1_ros2_demo/`
- container/tooling helpers: `tools/docker/isaac_lab_vla_stack/`
- docs page: `docs/examples/isaac-h1-ros2-demo.md`

This example keeps `muesli-bt` on the `Odometry` -> `Twist` ROS2 surface while Isaac Sim and NVIDIA's H1 controller handle the low-level humanoid policy loop.

## Isaac Sim Wheeled Demo

For a wheeled Isaac Sim example, use a small differential-drive robot with the same ROS 2 boundary:

- input: `nav_msgs/msg/Odometry`
- output: `geometry_msgs/msg/Twist`
- runtime entrypoint: `examples/repl_scripts/ros2-flagship-goal.lisp`
- docs page: `docs/examples/isaac-wheeled-ros2-showcase.md`
- example guide: [Isaac Sim: TurtleBot3 ROS2 Demo](docs/examples/isaac-wheeled-ros2-showcase.md)

This example gives you a simple path to run TurtleBot3 in Isaac Sim, keep the ROS 2 interface easy to understand, and record a repeatable simulator run.

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
- numeric helpers (`sqrt`, `log`, `exp`, `atan2`, `abs`, `clamp`)
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

### Capability and VLA layer

- capability registry (`cap.list`, `cap.describe`, `cap.call`)
- deterministic capability fixture (`cap.echo.v1`)
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
ctest --preset dev --output-on-failure
```

## Repository Layout

- `src/`, `include/`: core runtime, evaluator, BT engine, planner, VLA integration
- `integrations/`: optional backend integrations (for example `integrations/pybullet/`, `integrations/webots/`, `integrations/ros2/`)
- `examples/bt/`: compact BT scripts
- `examples/repl_scripts/`: end-to-end experiments and demos
- `examples/pybullet_racecar/`: racecar demo package
- `examples/pybullet_epuck_goal/`: differential-drive PyBullet surrogate for the shared wheeled flagship BT
- `examples/pybullet_racecar/native/`: demo bridge entrypoint that uses canonical `env.api.v1` via the PyBullet integration

- `tests/`: unit/integration coverage
- `docs/`: user and internals documentation

## Contributing

Contributions are welcome.

If you add or change a feature, update:

- user docs in `docs/`
- feature reference pages under `docs/language/reference/`
- `docs/docs-coverage.md` to keep reference coverage complete
