# muesli-bt

muesli-bt is a compact Lisp runtime with an integrated behaviour tree (BT) engine in modern C++.

It solves a common robotics and control problem: keeping task logic readable in scripts while preserving explicit runtime semantics and host-side integration points.

## Who It Is For

- robotics and autonomy engineers who want scriptable BT logic
- systems programmers embedding a small Lisp runtime in C++
- teams that need deterministic tests, traceability, and low integration overhead

## Recommended Start

1. Build and run a shipped BT script.

```bash
cmake --preset dev
cmake --build --preset dev -j
./build/dev/muslisp examples/bt/hello_bt.lisp
```

2. Open the REPL and run a small language sample.

```lisp
(begin
  (define x 10)
  (define y 2.5)
  (print (+ x y))
  (list 'ok 'ready))
```

## Manual Sections

- [Getting Started](getting-started.md): install/build/run workflow
- [Language](language/syntax.md): syntax, semantics, and complete reference
- [Behaviour Trees](bt/intro.md): DSL, runtime model, blackboard, scheduler, bounded-time planning, observability
- [Integration](bt/robot-integration.md): host callbacks and service wiring
- [Roadmap](limitations-roadmap.md): planned next features and priorities
