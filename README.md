# muesli-bt

[![Linux CI](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/linux-ci.yml)
[![Docs Pages](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml/badge.svg)](https://github.com/unswei/muesli-bt/actions/workflows/docs-pages.yml)
[![Docs](https://img.shields.io/badge/docs-online-brightgreen)](https://unswei.github.io/muesli-bt/)

A compact Lisp runtime with an integrated Behaviour Tree engine, bounded-time planning, and async vision-language-action orchestration.

`muslisp` is the executable. `muesli-bt` is the runtime and project.

## Why muesli-bt

When control loops need to stay responsive, three things matter:

- decision logic that stays readable (`bt`/`defbt` in Lisp)
- compute-heavy reasoning that respects strict tick budgets (`plan-action` / `planner.mcts`)
- asynchronous model calls that can stream and be cancelled (`vla.submit` / `vla.poll` / `vla.cancel`)

muesli-bt keeps those concerns in one place with explicit runtime semantics and built-in observability.

## Quick Start

Build:

```bash
cmake --preset dev
cmake --build --preset dev -j
```

Run a basic BT script:

```bash
./build/dev/muslisp examples/bt/hello_bt.lisp
```

Run bounded-time planner demo:

```bash
./build/dev/muslisp examples/repl_scripts/planner-bt-1d.lisp
```

Run pure Lisp A* grid-search demo:

```bash
./build/dev/muslisp examples/repl_scripts/a-star-grid.lisp
```

Run pure Lisp Dijkstra grid-search demo (using native `pq.*`):

```bash
./build/dev/muslisp examples/repl_scripts/dijkstra-grid-pq.lisp
```

Run pure Lisp PRM demo (deterministic seed + `pq.*` shortest path):

```bash
./build/dev/muslisp examples/repl_scripts/prm-2d-pq.lisp
```

Run async VLA demo with fallback behavior:

```bash
./build/dev/muslisp examples/repl_scripts/vla-stub-bt.lisp
```

Run the visual PyBullet racecar showcase:

```bash
make demo-setup
make demo-run MODE=bt_planner
```

Open a REPL:

```bash
./build/dev/muslisp
```

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
      (plan-action :name "mcts"
                   :state_key state
                   :action_key action
                   :budget_ms 10
                   :iters_max 300)
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

- DSL forms: `seq`, `sel`, `invert`, `repeat`, `retry`, `cond`, `act`, `succeed`, `fail`, `running`
- compile/load/save paths (`bt.compile`, `bt.to-dsl`, `bt.save`, `bt.load`, `bt.save-dsl`, `bt.load-dsl`)
- per-instance blackboard with typed values and write metadata
- scheduler-backed async actions

### Bounded-time planning

- `plan-action` node for strict per-tick budgeted planning
- `planner.mcts` service for direct scripted experiments
- progressive widening + UCB style search with deterministic seeding hooks
- structured planner stats and logs

### VLA capability layer

- capability registry (`cap.list`, `cap.describe`)
- async job API (`vla.submit`, `vla.poll`, `vla.cancel`)
- stream-aware polling (`:queued`, `:running`, `:streaming`, `:done`, `:error`, `:timeout`, `:cancelled`)
- opaque media handles (`image_handle`, `blob_handle`) plus metadata accessors
- structured per-job logs (JSONL sink + in-memory dump)

### Observability and control

- trace/log rings and dump APIs
- per-tree and per-node profiling stats
- scheduler stats and tick budget controls

## Documentation Map

- [Getting started](docs/getting-started.md)
- [Example: A* search](docs/examples/a-star-search.md)
- [Example: Dijkstra with PQ](docs/examples/dijkstra-pq.md)
- [Example: PRM with PQ](docs/examples/prm-pq.md)
- [Language manual](docs/language/syntax.md)
- [Language reference index](docs/language/reference/index.md)
- [Behaviour trees](docs/bt/intro.md)
- [Bounded-time planning](docs/bt/bounded-time-planning.md)
- [PlanAction node reference](docs/bt/plan-action-node.md)
- [VLA integration](docs/bt/vla-integration.md)
- [VLA nodes reference](docs/bt/vla-nodes.md)
- [VLA request/response schema](docs/bt/vla-request-response.md)
- [Observability](docs/observability/logging.md)
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

- `src/`, `include/`: runtime, evaluator, BT engine, planner, VLA integration
- `examples/bt/`: compact BT scripts
- `examples/repl_scripts/`: end-to-end experiments and demos
- `examples/pybullet_racecar/`: racecar demo package
  - `native/`: demo-only C++ + pybind bridge code for `env.pybullet.run-loop`
- `tests/`: unit/integration coverage
- `docs/`: user and internals documentation

## Contributing

Contributions are welcome.

If you add or change a feature, update:

- user docs in `docs/`
- feature reference pages under `docs/language/reference/`
- `docs/docs-coverage.md` to keep reference coverage complete
