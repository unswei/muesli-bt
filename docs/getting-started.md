# Getting Started

This page gets a new developer from clone to runnable REPL, tests, and docs.

## Toolchain Requirements

- C++20 compiler (`clang++` or `g++`)
- CMake 3.20+
- Ninja
- Python 3.11 (single environment for docs + `pybullet`)
- `uv` (recommended for Python environment management)

## Build And Test

From repository root:

```bash
cmake --preset dev
cmake --build --preset dev -j
ctest --preset dev
```

Without presets:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Run The REPL

```bash
./build/dev/muslisp
```

Exit with `:q`, `:quit`, or `:exit`.

Quick BT authoring example:

```lisp
(defbt patrol
  (sel
    (seq
      (cond target-visible)
      (act approach-target)
      (act grasp))
    (act search-target)))

(define inst (bt.new-instance patrol))
(bt.tick inst)
(bt.tick inst)
```

## Run Scripts

```bash
./build/dev/muslisp examples/repl_scripts/lisp-basics.lisp
./build/dev/muslisp examples/repl_scripts/a-star-grid.lisp
./build/dev/muslisp examples/repl_scripts/dijkstra-grid-pq.lisp
./build/dev/muslisp examples/repl_scripts/prm-2d-pq.lisp
./build/dev/muslisp examples/bt/hello_bt.lisp
./build/dev/muslisp examples/repl_scripts/planner-bt-1d.lisp
./build/dev/muslisp examples/repl_scripts/planner-bt-ptz.lisp
./build/dev/muslisp examples/repl_scripts/vla-stub-bt.lisp
./build/dev/muslisp examples/repl_scripts/hybrid-vla-mcts-1d.lisp
```

Notes:

- `dijkstra-grid-pq.lisp` prints `found`, total cost, path length, and step coordinates.
- `prm-2d-pq.lisp` uses a fixed seed and prints deterministic summary fields (`seed`, accepted nodes, edge count, `found`, path stats).
- Step-by-step walkthroughs:
  - [A* tutorial](examples/tutorials/a-star-step-by-step.md)
  - [Dijkstra tutorial](examples/tutorials/dijkstra-step-by-step.md)
  - [PRM tutorial](examples/tutorials/prm-step-by-step.md)

`(load "path/to/file.lisp")` is also available inside the REPL for runtime loading.

## Environment Capability Interface (`env.*`)

The runtime now exposes a backend-agnostic control surface:

- `env.info`, `env.attach`, `env.configure`
- `env.reset`, `env.observe`, `env.act`, `env.step`
- `env.run-loop`, `env.debug-draw`

Legacy `sim.*` names are temporary aliases to the same behaviour.

See the built-in reference index for exact signatures and error behaviour:

- [Language Reference Index](language/reference/index.md)

## Run Tests Directly

```bash
./build/dev/muslisp_tests
```

## Build Documentation

Create the unified Python environment (docs + `pybullet`):

```bash
./scripts/setup-python-env.sh
```

Serve locally:

```bash
.venv-py311/bin/python -m mkdocs serve
```

Build static docs:

```bash
.venv-py311/bin/python -m mkdocs build
```

## Where Examples Live

- runnable Lisp scripts: `examples/repl_scripts/` and `examples/bt/`
- prose walkthroughs: `docs/examples/`

## Common Issues

- `ninja: command not found`
: install Ninja (`brew install ninja` or `sudo apt install ninja-build`).

- `mkdocs: command not found`
: use the unified environment: `./scripts/setup-python-env.sh`.

- wrong compiler selected
: set `CC` and `CXX` before configuring.

```bash
CC=clang CXX=clang++ cmake --preset dev
```
