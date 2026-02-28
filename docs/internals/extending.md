# Extension Guide

This page describes safe extension patterns for contributors.

## Add A New Lisp Built-in

Checklist:

1. implement builtin function in `src/builtins.cpp`
2. enforce arity and type checks
3. bind in `install_core_builtins`
4. add tests for happy path and error cases
5. document in `docs/language/builtins.md`

Notes:

- Generic environment capability builtins (`env.*`, `sim.*` alias layer) live in `src/env_builtins.cpp`.
- Backend registration/attachment state for `env.*` lives in `src/env_api.cpp`.

## Add A New BT DSL (domain-specific language) Node/Decorator

Checklist:

1. add node kind to `include/bt/ast.hpp`
2. compile syntax and checks in `src/bt/compiler.cpp`
3. add runtime semantics in `src/bt/runtime.cpp`
4. update trace/profile output if needed
5. add compile/runtime tests
6. document syntax and semantics pages

For async capability leaves (`vla-request`, `vla-wait`, `vla-cancel`), keep tick calls non-blocking and route all external work through scheduler-owned services.

## Add A New Blackboard Value Type

Checklist:

1. extend `bb_value` variant in `include/bt/blackboard.hpp`
2. update value rendering in `src/bt/blackboard.cpp`
3. update dump/trace printing paths
4. test read/write/dump behaviour
5. document type and usage constraints

## Add Host Callbacks

Checklist:

1. register condition/action in `src/bt/runtime_host.cpp` (or host app)
2. keep callback contracts explicit (input args, side effects, status)
3. use `node_memory` for multi-tick actions
4. use blackboard writes deliberately with stable key naming
5. add integration tests with deterministic outcomes

## Add/Extend Capabilities

Checklist:

1. update capability descriptors in `src/bt/vla.cpp`
2. keep request/response schemas explicit and validated
3. ensure submit/poll/cancel remains non-blocking from BT ticks
4. add logging/replay coverage
5. document Lisp-facing builtins and BT node usage

## Extend Tracing

Checklist:

1. add event kind/fields in `include/bt/trace.hpp`
2. emit events at precise semantics boundaries
3. keep trace bounded and readable
4. update dump formatting and tests
5. document workflows in observability pages

## Testing Expectations For Changes

For any semantic change, add at least:

- one direct runtime test
- one negative or edge-case test
- docs update for user-visible behaviour
