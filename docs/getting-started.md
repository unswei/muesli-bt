# Getting Started

This page gets a new developer from clone to runnable REPL, tests, and docs.

## Toolchain Requirements

- C++20 compiler (`clang++` or `g++`)
- CMake 3.20+
- Ninja
- Python 3 (for MkDocs docs build)

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

## Run A Script

```bash
./build/dev/muslisp examples/repl_scripts/lisp-basics.lisp
```

## Run Tests Directly

```bash
./build/dev/muslisp_tests
```

## Documentation Build

Install docs dependencies:

```bash
python3 -m pip install -r docs/requirements.txt
```

Serve locally:

```bash
mkdocs serve
```

Build static docs:

```bash
mkdocs build
```

## Where Examples Live

- runnable Lisp scripts: `examples/repl_scripts/` and `examples/bt/`
- prose walkthroughs: `docs/examples/`

## Common Issues

- `ninja: command not found`
: install Ninja (`brew install ninja` or `sudo apt install ninja-build`).

- `mkdocs: command not found`
: install docs dependencies with `python3 -m pip install -r docs/requirements.txt`.

- wrong compiler selected
: set `CC` and `CXX` before configuring.

```bash
CC=clang CXX=clang++ cmake --preset dev
```
