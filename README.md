# muslisp-bt

A compact Lisp runtime with built-in Behaviour Tree (BT) compile/tick support, written in modern C++20.

This repository currently covers:

- Phase 1: Lisp core
- Phase 2: non-moving mark/sweep GC
- Phase 3: BT compiler + runtime core nodes
- Phase 4: decorators + per-node instance memory + reset
- Phase 5: inspectability (blackboard metadata, trace, logs, stats)

## What Works

### Lisp core

Implemented value types:

- `nil`
- `boolean`
- `integer` (`std::int64_t`)
- `float` (`double`)
- `symbol`
- `string`
- `cons`
- `primitive_fn`
- `closure`
- `bt_def`
- `bt_instance`

Reader/parser supports:

- integers and float literals (`123`, `-42`, `3.14`, `1e-3`, `2.`)
- symbols
- booleans (`#t`, `#f`)
- lists
- quote sugar (`'x`)
- strings with escapes
- line comments (`; ...`)

Evaluator supports:

- `quote`
- `if`
- `define`
- `lambda`
- `begin`

Closures use lexical scope with environment chaining.

### Numeric model (int + float)

- `+`, `-`, `*`, `/`, `=`, `<`, `>`, `<=`, `>=`
- mixed int/float promotion
- `/` always returns float
- checked integer overflow for int arithmetic
- predicates: `number?`, `int?`, `integer?`, `float?`, `zero?`
- float printing includes readable forms like `2.0`, `3.14`, `inf`, `-inf`, `nan`

### Memory management

- non-moving stop-the-world mark/sweep GC
- GC-managed Lisp objects and environments
- root registration for global env + scoped temporaries during eval
- threshold-based collection trigger

GC/heap built-ins:

- `(heap-stats)`
- `(gc-stats)` (forces a collection before printing)

Both print:

- total allocated objects
- live objects after last GC
- bytes allocated
- next GC threshold

### BT DSL compiler

`(bt.compile '<dsl-form>)` compiles quoted BT DSL into an internal node graph.

Supported DSL forms:

- composites: `(seq child...)`, `(sel child...)`
- decorators: `(invert child)`, `(repeat n child)`, `(retry n child)`
- leaves: `(cond name arg...)`, `(act name arg...)`
- utility nodes: `(succeed)`, `(fail)`, `(running)`

Compiler validation includes:

- known form checks
- arity checks
- non-empty child checks for `seq`/`sel`
- integer validation for decorator counts
- leaf name must be symbol/string
- leaf args limited to literal/symbol forms

### BT runtime semantics

- statuses: `success`, `failure`, `running`
- memoryless `seq` and `sel` traversal
- decorator semantics for `invert`, `repeat`, `retry`
- per-node runtime memory (`node_memory`)
- instance-level blackboard storage with metadata
- `(bt.reset inst)` clears per-node memory and blackboard

### Blackboard, tracing, logging, profiling, scheduler

Blackboard:

- typed variant values (`nil`, bool, int64, double, string)
- metadata on writes:
  - `last_write_tick`
  - `last_write_ts`
  - `last_writer_node_id`
  - `last_writer_name`

Trace:

- in-memory ring buffer
- events include:
  - `tick_begin`, `tick_end`
  - `node_enter`, `node_exit`
  - `bb_write`
  - optional `bb_read` (via `(bt.set-read-trace-enabled inst #t)`)
  - `scheduler_submit`, `scheduler_start`, `scheduler_finish`, `scheduler_cancel`
  - `warning`, `error`

Logging:

- in-memory ring log sink
- per-record level/category/tick/node/message fields

Profiling:

- per-tree tick stats
- per-node duration and return counters
- scheduler queue/run timing stats
- tick budget warnings via `(bt.set-tick-budget-ms inst ms)`

Scheduler:

- fixed-size thread pool scheduler
- async job submit/poll/result/cancel API
- built-in demo async action (`async-sleep-ms`)

### Lisp-facing BT built-ins

Core:

- `bt.compile`
- `bt.new-instance`
- `bt.tick`
- `bt.reset`
- `bt.status->symbol`

Companion-style introspection/config:

- `bt.stats`
- `bt.trace.dump`
- `bt.trace.snapshot`
- `bt.blackboard.dump`
- `bt.logs.dump`
- `bt.logs.snapshot`
- `bt.scheduler.stats`
- `bt.set-tick-budget-ms`
- `bt.set-trace-enabled`
- `bt.set-read-trace-enabled`
- `bt.clear-trace`
- `bt.clear-logs`

`bt.tick` supports:

- `(bt.tick inst)`
- `(bt.tick inst '((key value) ...))` to seed/update blackboard entries before the tick

### Demo host callbacks installed by default

Conditions:

- `always-true`
- `always-false`
- `bb-has`

Actions:

- `always-success`
- `always-fail`
- `running-then-success`
- `bb-put-int`
- `bb-put-float`
- `async-sleep-ms`

## Build and Test (macOS/Linux)

Requirements:

- C++20 compiler (`clang++` or `g++`)
- CMake 3.20+
- Ninja (recommended)

With presets:

```bash
cmake --preset dev
cmake --build --preset dev -j
ctest --preset dev
```

Without presets:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run REPL:

```bash
./build/dev/muslisp
```

Run script:

```bash
./build/dev/muslisp path/to/script.lisp
```

## Verification Status

Current automated tests cover:

- reader/parser
- evaluator and lexical closures
- int/float arithmetic semantics and predicates
- integer overflow handling
- GC behavior and GC stats built-ins
- GC safety during argument evaluation with in-eval collection
- BT compiler checks
- BT runtime status propagation and decorators
- blackboard writes/reads and trace events
- scheduler-backed async action behavior
- BT introspection/config built-ins
- `bt.tick` blackboard input path

Validated in this environment with:

- AppleClang 17 (macOS)
- GNU g++-15 (Homebrew toolchain on macOS)

Code is written to be portable across Linux/macOS (standard library threads/chrono/containers, no platform-specific APIs).

## Not Implemented Yet

- timeout decorator (`timeout`) from companion extension ideas
- external logging/metrics adapters (for example `spdlog`, Prometheus, OpenTelemetry)
- macro-based BT authoring sugar (`(bt ...)`)
- advanced halting contracts for long-running leaves beyond reset/cancel patterns
