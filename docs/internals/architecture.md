# Project Architecture

## Module Boundaries

## Lisp runtime

- value model
- reader/parser
- evaluator
- environment
- built-ins
- GC

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
