# muesli-bt

muesli-bt is a compact Lisp runtime with built-in Behaviour Tree (BT) compile/tick support, written in modern C++20.

The `muslisp` executable is the Lisp interpreter. The project/runtime name is `muesli-bt`.

Full documentation lives in `docs/` and can be served locally with `mkdocs serve`.

## 5-Minute Try It

Build and run:

```bash
cmake --preset dev
cmake --build --preset dev -j
./build/dev/muslisp
```

Inside the REPL:

```lisp
(define t
  (bt.compile
    '(sel
       (seq
         (cond target-visible)
         (act approach-target)
         (act grasp))
       (act search-target))))

(define inst (bt.new-instance t))
(bt.tick inst)
(bt.tick inst)
(bt.stats inst)
(bt.blackboard.dump inst)
(bt.trace.dump inst)
```

`bt.tick` returns a Lisp symbol: `success`, `failure`, or `running`.

## Architecture Sketch

```text
+----------------------------------+
| Lisp reader/eval/GC (`muslisp`)  |
+-------------------+--------------+
                    |
                    v
+----------------------------------+
| BT DSL (domain-specific language) compiler (`bt.compile`)   |
| BT runtime + instance state      |
| blackboard + trace + profiling   |
+-------------------+--------------+
                    |
                    v
+----------------------------------+
| C++ registry + host services     |
| scheduler + clock + robot hooks  |
+----------------------------------+
```

## Intended Usage Model

- BTs are authored as quoted BT DSL forms and compiled with `bt.compile`.
- A host supervision loop owns tick cadence and calls `bt.tick`.
- Conditions/actions are implemented in C++ and registered in the runtime host.
- Blackboard keys carry task/runtime state and are inspected via dump/trace surfaces.
- Async work is delegated to the scheduler, and results are reconciled on a later tick.

## Design Choices (v1)

- Non-moving mark/sweep GC keeps C++ interop simple and pointer-stable.
- BTs compile from quoted Lisp data in v1; macro sugar is intentionally deferred.
- `seq`/`sel` are memoryless in v1 to keep semantics explicit and testable.
- Explicit leaf `halt` contracts are deferred to v2 in favour of a smaller initial runtime.

## Current Scope

This repository currently covers:

- Phase 1: Lisp core
- Phase 2: non-moving mark/sweep GC
- Phase 3: BT compiler + runtime core nodes
- Phase 4: decorators + per-node instance memory + reset
- Phase 5: inspectability (blackboard metadata, trace, logs, stats)
- Phase 6: robotics-oriented host integration (typed services + sample wrappers)

## What Works

### Core Runtime

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

Numeric model:

- `+`, `-`, `*`, `/`, `=`, `<`, `>`, `<=`, `>=`
- mixed int/float promotion
- `/` always returns float
- checked integer overflow for int arithmetic
- predicates: `number?`, `int?`, `integer?`, `float?`, `zero?`

GC:

- non-moving stop-the-world mark/sweep collector
- GC-managed Lisp objects and environments
- threshold-based collection trigger
- built-ins: `(heap-stats)`, `(gc-stats)`

BT compile/runtime:

- `(bt.compile '<dsl-form>)` for BT DSL compilation
- composites: `seq`, `sel`
- decorators: `invert`, `repeat`, `retry`
- leaves: `cond`, `act`
- utility nodes: `succeed`, `fail`, `running`
- runtime statuses: `success`, `failure`, `running`
- memoryless `seq`/`sel` traversal
- per-node runtime memory (`node_memory`)
- `(bt.reset inst)` clears per-node memory and blackboard

Typed host services (Phase 6):

- `scheduler*`
- observability handles (`trace_buffer*`, `log_sink*`)
- `clock_interface*`
- `robot_interface*`

Default demo callbacks include:

- conditions: `always-true`, `always-false`, `bb-has`, `battery-ok`, `target-visible`
- actions: `always-success`, `always-fail`, `running-then-success`, `bb-put-int`, `bb-put-float`, `async-sleep-ms`, `approach-target`, `grasp`, `search-target`

### Operations And Observability

Blackboard:

- typed values: `nil`, bool, int64, double, string
- write metadata: tick/time/writer node/writer name
- dump API: `bt.blackboard.dump`

Trace:

- per-instance bounded in-memory ring buffer
- events: `tick_begin`, `tick_end`, `node_enter`, `node_exit`, `bb_write`, optional `bb_read`, scheduler lifecycle events, `warning`, `error`
- APIs: `bt.trace.dump`, `bt.trace.snapshot`, `bt.clear-trace`, `bt.set-trace-enabled`, `bt.set-read-trace-enabled`

Logging:

- bounded in-memory ring sink (`memory_log_sink`)
- record fields: sequence, `ts_ns`, level, tick, node, category, message
- APIs: `bt.logs.dump`, `bt.logs.snapshot`, `bt.log.dump`, `bt.log.snapshot`, `bt.clear-logs`

Profiling:

- per-tree tick duration/counters and tick overrun counters
- per-node duration and return counters
- scheduler queue/run timing counters
- APIs: `bt.stats`, `bt.scheduler.stats`, `bt.set-tick-budget-ms`

Scheduler:

- fixed-size thread pool (`thread_pool_scheduler`)
- async submit/poll/result/cancel C++ API
- Lisp-facing visibility via `bt.scheduler.stats`
- built-in scheduler-backed action example: `async-sleep-ms`

## Lisp-Facing BT Built-ins

Core:

- `bt.compile`
- `bt.new-instance`
- `bt.tick`
- `bt.reset`
- `bt.status->symbol`

Inspectability/config:

- `bt.stats`
- `bt.trace.dump`
- `bt.trace.snapshot`
- `bt.blackboard.dump`
- `bt.logs.dump`
- `bt.logs.snapshot`
- `bt.log.dump`
- `bt.log.snapshot`
- `bt.scheduler.stats`
- `bt.set-tick-budget-ms`
- `bt.set-trace-enabled`
- `bt.set-read-trace-enabled`
- `bt.clear-trace`
- `bt.clear-logs`

`bt.tick` supports:

- `(bt.tick inst)`
- `(bt.tick inst '((key value) ...))` to seed/update blackboard entries before the tick

## Threading Model And Safety Boundaries

Current model is mixed:

- BT ticking and Lisp evaluation are expected on an owning host thread.
- `thread_pool_scheduler` executes background job functions on worker threads.
- Per-instance BT state (`node_memory`, blackboard, trace buffer, profile counters) is mutated on tick.
- Scheduler workers should not mutate BT instance state directly.
- Async job outcomes should be applied from the tick path (for example inside an action on a later tick).

Important v1 boundary:

- explicit `halt` semantics are not implemented
- cancellation is best-effort where scheduler jobs use it
- host code should avoid concurrent `bt.tick` calls on the same instance

## Build, Test, And Docs (macOS/Linux)

Requirements:

- C++20 compiler (`clang++` or `g++`)
- CMake 3.20+
- Ninja (recommended)
- Python 3 (for docs)

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

Serve docs:

```bash
python3 -m pip install -r docs/requirements.txt
mkdocs serve
```

## Verification Status

Current automated tests cover:

- reader/parser
- evaluator and lexical closures
- int/float arithmetic semantics and predicates
- integer overflow handling
- GC behaviour and GC stats built-ins
- GC safety during argument evaluation with in-eval collection
- BT compiler checks
- BT runtime status propagation and decorators
- phase-6 sample wrappers (`battery-ok`, `target-visible`, `approach-target`, `grasp`, `search-target`)
- typed custom robot interface injection
- blackboard writes/reads and trace events
- scheduler-backed async action behaviour
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
- memoryful composite variants (`mem-seq`, `mem-sel`)

## Next Steps For Logging And Profiling

- add filtered dump helpers (for example by level/category/tick range)
- add persistent/export sinks so logs and traces can leave process memory
- expose richer scheduler metrics (`queue_overflow`, max/total timings, histograms)
- add percentile-oriented profiling output for tick and node timings
- add a stable machine-readable stats output mode in addition to plain text
