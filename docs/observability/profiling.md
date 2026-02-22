# Profiling And Performance

v1 profiling is intentionally lightweight but useful.

## What To Measure First

- per-tick duration
- per-node durations
- counts of `running`/`success`/`failure` returns
- scheduler queue and run timings
- GC pressure indicators

## Runtime Profiling Surfaces

## BT stats

```lisp
(bt.stats inst)
```

Includes tick counters and node-level timing summaries.

## Scheduler stats

```lisp
(bt.scheduler.stats)
```

Includes submitted/started/completed/failed/cancelled plus timing fields.

## GC/Heap stats

```lisp
(heap-stats)
(gc-stats)
```

Printed counters include:

- total allocated objects
- live objects after last GC
- bytes allocated
- next GC threshold

## Budget Warnings

Set warning budget:

```lisp
(bt.set-tick-budget-ms inst 20)
```

If tick duration exceeds budget, runtime emits warning trace/log and increments overrun counters.

## Tracing Overhead Guidance

Tracing is valuable but not free.

Suggested workflow:

1. profile with trace mostly off
2. enable tracing when investigating a specific issue
3. disable `bb_read` trace once diagnosis is complete

## Debug vs Release

- Debug builds are preferred for semantics and instrumentation validation.
- Release builds are preferred for realistic throughput measurements.
