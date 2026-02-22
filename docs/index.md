# muesli-bt

muesli-bt is a tiny Lisp runtime with built-in behaviour tree (BT) support, implemented in modern C++.

The project exists to provide:

- clear BT authoring in Lisp data
- explicit, testable BT semantics (`success`, `failure`, `running`)
- practical host-side C++ integration for robotics work

## What It Is For

- prototyping task-level robot logic
- integrating host callbacks for conditions and actions
- inspecting execution with trace, logs, and blackboard dumps
- extending a compact runtime without heavy dependencies

## What It Is Not For

- macro-heavy general Lisp development
- hard real-time guarantees
- full BT halting semantics in v1
- visual BT editing pipelines

## v1 Scope

Implemented in v1 (phases 1-6):

- Lisp core (reader, evaluator, closures, core built-ins)
- non-moving mark/sweep GC
- BT compiler and runtime core
- decorators and per-node instance memory
- inspectability (blackboard metadata, trace, logs, stats)
- typed host services with sample robotics wrappers

Planned later (v2+):

- explicit `halt` contracts for leaves
- macro conveniences such as `(bt ...)`
- memoryful sequence/selector variants

## Architecture Sketch

```text
+-------------------------------+
| Lisp Layer                    |
| - reader / parser             |
| - evaluator / closures        |
| - built-ins (bt.* included)   |
+---------------+---------------+
                |
                v
+-------------------------------+
| BT Layer                      |
| - bt.compile (DSL -> nodes)   |
| - tick runtime                |
| - instance memory             |
| - blackboard                  |
| - trace / profiling           |
+---------------+---------------+
                |
                v
+-------------------------------+
| Host Integration Layer        |
| - condition/action registry   |
| - typed services              |
| - scheduler / clock / robot   |
+-------------------------------+
```

## First BT Example

```lisp
(define tree
  (bt.compile
    '(sel
       (seq
         (cond target-visible)
         (act approach-target)
         (act grasp))
       (act search-target))))

(define inst (bt.new-instance tree))
(bt.tick inst)
```

Next: [Getting Started](getting-started.md).
