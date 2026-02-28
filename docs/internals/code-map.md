# Code Map

Use this page to find where to implement specific changes.

## Key Folders

- `include/` public headers
- `src/` implementation
- `tests/` test binary sources
- `.github/workflows/` CI jobs

## Core Files

### Lisp runtime

- `include/muslisp/value.hpp`
- `src/reader.cpp`
- `src/eval.cpp`
- `src/builtins.cpp`
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

## Where Do I Change X?

- add Lisp builtin: `src/builtins.cpp`
- add BT DSL (domain-specific language) node/decorator: `include/bt/ast.hpp`, `src/bt/compiler.cpp`, `src/bt/runtime.cpp`, tests
- add blackboard value type: `include/bt/blackboard.hpp`, `src/bt/blackboard.cpp`, dump/trace paths, tests
- add trace field/event: `include/bt/trace.hpp`, `src/bt/runtime.cpp`, dump helpers, tests
- add host wrappers/services: `include/bt/instance.hpp`, `include/bt/runtime_host.hpp`, `src/bt/runtime_host.cpp`, tests
- add capability/VLA job service: `include/bt/vla.hpp`, `src/bt/vla.cpp`, `src/builtins.cpp`, tests
