# Getting Started

This page gets a new developer from clone to a verified local build, a working REPL, and one first runnable script.
It is written for the shortest path to first success, not for full architecture or language coverage.
If you only need to confirm that the repository builds and the runtime works, you can stop after the install verification step.
If you want to understand the BT example on this page, follow the linked Lisp and BT introductions immediately afterwards.

If you are new to the architecture, read [What Is muesli-bt](getting-oriented/what-is-muesli-bt.md) first.

## What You Will Have Working After This Page

- a successful local build and test run
- a verified install path that emits the canonical event log
- a working `muslisp` REPL
- one recommended first script to run next
- a clear path into the Lisp, BT, and example docs

## Toolchain Requirements

- C++20 compiler (`clang++` or `g++`)
- CMake 3.20+
- Ninja
- Python 3.11 for the unified docs + `pybullet` environment (this can be provided by `uv` even if the host system Python is older)
- `uv` (recommended for Python environment management)
- Graphviz `dot` (required for rendering architecture/BT diagrams in docs)

## Build And Test

From repository root:

```bash
cmake --preset dev
cmake --build --preset dev -j
ctest --preset dev
```

Core-only boundary check (no optional integrations):

```bash
cmake --preset core-only
cmake --build --preset core-only -j
ctest --preset core-only
```

The `core-only` preset explicitly keeps `MUESLI_BT_BUILD_INTEGRATION_ROS2=OFF`, so this path remains portable across supported non-ROS developer hosts such as Linux and macOS.

Install verification (canonical event stream + schema validation):

```bash
make demo-setup
make verify-install
```

`make verify-install` writes `logs/verify-install.mbt.evt.v1.jsonl` and validates it
against `schemas/event_log/v1/mbt.evt.v1.schema.json`.

If you only needed a verified install, you can stop here and use the repository normally.
If you want a first readable example, continue with the REPL and the recommended script below.

Without presets:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Consume As A Package

If you are embedding `muesli-bt` into another CMake project, use:

- [consume as an installed package](getting-started-consume.md)

That page covers `cmake --install`, exported targets, optional integrations, and
the Studio contract assets shipped in `${prefix}/share/muesli_bt`.

## Run The REPL

```bash
./build/dev/muslisp
```

Exit with `:q`, `:quit`, or `:exit`.

REPL notes:

- Interactive Linux and macOS terminals use a small built-in line editor for current-line editing, history, and wrapped input.
- Persistent REPL history is stored at `~/.muesli_bt_history`.
- Use `:clear` to drop a pending multi-line buffer without leaving the REPL.

## Run A First Script

Recommended first script:

```bash
./build/dev/muslisp examples/repl_scripts/lisp-basics.lisp
```

Use this first if you are new to Lisp syntax.
The matching walkthrough page is [Example: muslisp Basics](examples/lisp-basics.md).

## Before You Read The BT Snippet

If you are new to Lisp or BTs, treat the next snippet as a quick shape check rather than required knowledge for installation.

- Read [Brief Lisp Introduction](lisp-basics.md) for the minimum language needed to read it.
- Read [Brief Behaviour Tree Introduction](bt/intro.md) for the first BT forms and statuses.
- Run [Example: Hello BT](examples/hello-bt.md) if you want the same ideas explained tick by tick.

In the snippet below, `target-visible`, `approach-target`, `grasp`, and `search-target` are host callback names, not built-in Lisp keywords.

## First BT Snippet

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

Other runnable scripts:

```bash
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

## Script File Extension Convention

Use `.lisp` for muslisp/muesli-bt scripts.

There is no semantic difference between `.lisp`, `.muslisp`, or older `.mueslisp` naming; file extension is a naming convention only. The project standard is now `.lisp`.

## Environment Capability Interface (`env.*`)

The runtime now exposes a backend-agnostic control surface:

- `env.info`, `env.attach`, `env.configure`
- `env.reset`, `env.observe`, `env.act`, `env.step`
- `env.run-loop`, `env.debug-draw`

See the built-in reference index for exact signatures and error behaviour:

- [Language Reference Index](language/reference/index.md)

If you want the control-loop picture before the detailed reference, read [How Execution Works](getting-oriented/how-execution-works.md).

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

Diagram rendering is automatic during docs build through a pre-build hook.
You can also run it directly:

```bash
python3 scripts/render-doc-diagrams.py
```

## Where Examples Live

- runnable Lisp scripts: `examples/repl_scripts/` and `examples/bt/`
- prose walkthroughs: `docs/examples/`
- simulator/robot integrations: [Integration chapter](integration/overview.md)

## Recommended Next Pages

- [What Is muesli-bt](getting-oriented/what-is-muesli-bt.md) for the runtime pieces and overall scope
- [Brief Lisp Introduction](lisp-basics.md) for the minimum language needed in the first examples
- [Brief Behaviour Tree Introduction](bt/intro.md) for the first composite and leaf forms
- [Example: Hello BT](examples/hello-bt.md) for the first tiny tree with expected tick outcomes
- [Examples Overview](examples/index.md) for the broader learning path

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
