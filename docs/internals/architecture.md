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

- DSL compiler
- compiled node graph
- tick semantics
- instance memory
- blackboard
- trace/log/profile hooks

## Host integration

- callback registry
- scheduler
- typed clock/robot service interfaces

## Dependency Direction

Preferred direction:

- Lisp layer calls BT built-ins
- BT runtime does not depend on REPL specifics
- host integration plugs into BT runtime through typed interfaces

## Data Flow

1. user writes quoted BT DSL in Lisp
2. `bt.compile` creates immutable definition
3. `bt.new-instance` creates mutable instance
4. host ticks instance repeatedly
5. leaves call host callbacks and blackboard
6. trace/log/stats provide inspectability
