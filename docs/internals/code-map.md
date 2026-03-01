# Code Map

Use this page to find where to implement specific changes.

## Key Folders

- `include/` public headers
- `src/` implementation
- `examples/` runnable Lisp and demo packages
- `tests/` test binary sources
- `.github/workflows/` CI jobs

## Core Files

### Lisp runtime

- `include/muslisp/value.hpp`
- `src/reader.cpp`
- `src/eval.cpp`
- `src/builtins.cpp`
- `src/env_builtins.cpp`
- `src/env_api.cpp`
- `src/gc.cpp`

### BT runtime

- `include/bt/ast.hpp`
- `src/bt/compiler.cpp`
- `src/bt/runtime.cpp`
- `src/bt/blackboard.cpp`
- `src/bt/trace.cpp`
- `src/bt/logging.cpp`
- `src/bt/scheduler.cpp`
- `src/bt/runtime_host.cpp`
- `src/bt/planner.cpp`
- `src/bt/vla.cpp`

### Entry points

- `src/main.cpp` (REPL + script mode)
- `tests/test_main.cpp` (coverage)

### Demo integration (PyBullet racecar)

- `examples/pybullet_racecar/run_demo.py` (demo entrypoint)
- `examples/pybullet_racecar/native/racecar_demo.cpp` (demo builtins/callbacks/run-loop)
- `examples/pybullet_racecar/native/bridge_module.cpp` (Python bridge module)

## Where Do I Change X?

- add Lisp builtin: `src/builtins.cpp` (general builtins) or `src/env_builtins.cpp` (`env.*` capability interface)
- add BT language (DSL: a small purpose-built language for behaviour trees) node/decorator: `include/bt/ast.hpp`, `src/bt/compiler.cpp`, `src/bt/runtime.cpp`, tests
- add blackboard value type: `include/bt/blackboard.hpp`, `src/bt/blackboard.cpp`, dump/trace paths, tests
- add trace field/event: `include/bt/trace.hpp`, `src/bt/runtime.cpp`, dump helpers, tests
- add [host](../terminology.md#host) wrappers/services: `include/bt/instance.hpp`, `include/bt/runtime_host.hpp`, `src/bt/runtime_host.cpp`, tests
- add capability/VLA job service: `include/bt/vla.hpp`, `src/bt/vla.cpp`, `src/builtins.cpp`, tests
- add `env.*` backend adapter plumbing: `include/muslisp/env_api.hpp`, `src/env_api.cpp`, `src/env_builtins.cpp`, backend extension files
