# known limitations

This page states the current project boundaries directly. Use it alongside the [current maturity table](index.md#current-maturity), [release notes](releases/index.md), and [roadmap to 1.0](roadmap-to-1.0.md).

## not a hard real-time controller

`muesli-bt` is intended for task-level control. Servo loops and safety-critical low-level control should remain in appropriate real-time systems.

## VLA production backends

The runtime has async/model-call lifecycle support. Production VLA provider integration is released only when a concrete backend is documented and tested.

## ROS2 support

ROS2 support is a thin host integration layer. It does not redefine core BT semantics.

## Nav2 and MoveIt

Nav2 and MoveIt adapters are roadmap items unless listed as released in the status table or release notes.

## Lisp authoring

The Lisp layer makes runtime task logic inspectable and compact, but it has a learning curve. The repository includes Lisp basics and BT examples to reduce that cost.

## GC and allocation

GC and allocation behaviour are measured and controlled at the task-runtime level. This does not make the runtime a hard real-time collector.

## benchmarks

Benchmark results depend on workload, hardware, compiler, and build flags. Use curated evidence bundles for paper or external comparison.

## host-side safety

Host capabilities own robot IO, external service calls, and safety-critical execution. Lisp BT logic should orchestrate task-level decisions under the runtime contract, not bypass host-side validation.
