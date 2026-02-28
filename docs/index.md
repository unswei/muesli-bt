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

3. Run complete algorithm examples (A*, Dijkstra+PQ, PRM+PQ).

```bash
./build/dev/muslisp examples/repl_scripts/a-star-grid.lisp
./build/dev/muslisp examples/repl_scripts/dijkstra-grid-pq.lisp
./build/dev/muslisp examples/repl_scripts/prm-2d-pq.lisp
```

## Manual Sections

- [Getting Started](getting-started.md): install/build/run workflow
- [A* Search Example](examples/a-star-search.md): full muslisp implementation
- [A* Tutorial (Step By Step)](examples/tutorials/a-star-step-by-step.md): implementation walkthrough
- [Dijkstra + PQ Example](examples/dijkstra-pq.md): shortest path with native priority queue
- [Dijkstra Tutorial (Step By Step)](examples/tutorials/dijkstra-step-by-step.md): implementation walkthrough
- [PRM + PQ Example](examples/prm-pq.md): simulator-free roadmap planning in pure muslisp
- [PRM Tutorial (Step By Step)](examples/tutorials/prm-step-by-step.md): implementation walkthrough
- [Language Built-ins Overview](language/builtins.md): includes `env.*` capability interface summary
- [Language](language/syntax.md): syntax, semantics, and complete reference
- [Behaviour Trees](bt/intro.md): DSL, runtime model, blackboard, scheduler, bounded-time planning, observability
- [VLA Integration](bt/vla-integration.md): capability-based async vision-language-action orchestration
- [Integration](bt/robot-integration.md): host callbacks and service wiring
- [Roadmap](limitations-roadmap.md): planned next features and priorities
