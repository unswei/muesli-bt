# Project Architecture

## Module Boundaries

## Lisp runtime

- value model
- reader/parser
- evaluator
- environment
- built-ins
- GC

## Evaluator execution paths

The Lisp evaluator now has two internal execution paths for closures:

- the default tree-walking evaluator in `src/eval.cpp`
- an optional compiled-closure path in `src/compiled_eval.cpp`

This is an internal optimisation only. Public Lisp semantics stay the same:

- closures still capture lexical environment
- argument checks still happen at call time
- unsupported forms still evaluate correctly through the existing evaluator

The current compiled subset is intentionally small. It handles:

- literals and symbol lookup
- ordinary function calls
- `quote`
- `if`
- `begin`
- `let`
- `and`
- `or`

If a closure body uses forms outside that subset, the closure simply keeps using the tree-walking evaluator. Current examples that fall back include:

- `cond`
- nested `lambda`
- `load`
- BT forms such as `bt` and `defbt`
- quasiquote forms

Tail-position execution is now explicit in `src/eval.cpp`. Tail calls bounce through an internal loop instead of recurring through the host C++ stack, so deep self recursion and mutual recursion stay bounded by runtime state rather than native stack depth. Compiled closures do the same thing inside `execute_compiled_closure(...)` through a `tail_call` opcode that reuses the active closure/frame state.

GC rooting follows that execution model:

- environment roots behave like a stack as tail calls change scope
- compiled evaluation roots literals, locals, and stack values explicitly
- GC polling happens periodically during long tail-call bounce chains rather than on every hop

## BT runtime

- BT language compiler (DSL: a small purpose-built language for behaviour trees)
- compiled node graph
- tick semantics
- instance memory
- blackboard
- trace/log/profile hooks

## [Host](../terminology.md#host) integration

- callback registry
- scheduler
- typed clock/robot service interfaces
- planner and VLA services

## Dependency Direction

Preferred direction:

- Lisp layer calls BT built-ins
- BT runtime does not depend on REPL specifics
- host integration plugs into BT runtime through typed interfaces

## Data Flow

1. user writes BT language in Lisp (`bt`/`defbt` or quoted form with `bt.compile`)
2. `bt`/`defbt` (or `bt.compile`) create immutable definition
3. `bt.new-instance` creates mutable instance
4. host ticks instance repeatedly
5. leaves call host callbacks and blackboard
6. planner/VLA services provide bounded-time and async decision APIs
7. trace/log/stats provide inspectability
