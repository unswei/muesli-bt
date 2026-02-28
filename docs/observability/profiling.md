# Profiling And Performance

This page documents the profiling surfaces available today.

## Available Features

- per-tree tick duration and overrun counters
- per-node duration and return-status counters
- scheduler throughput/timing counters
- configurable tick budget warnings
- GC/heap counters for allocation pressure context

## Commands Available Now

| Command | Purpose | Return |
| --- | --- | --- |
| `(bt.stats inst)` | Tree + node runtime stats | string |
| `(bt.scheduler.stats)` | Scheduler counters/timings | string |
| `(planner.logs.dump [n])` | Planner per-call records (JSON lines text) | string |
| `(bt.set-tick-budget-ms inst ms)` | Set tick overrun budget | `nil` |
| `(heap-stats)` | Heap counters snapshot | `nil` (prints) |
| `(gc-stats)` | Force GC then print counters | `nil` (prints) |

## `bt.stats` Output (What It Contains)

`bt.stats` includes at least:

- `tick_count`
- `tick_overrun_count`
- `tick_last_ns`
- `tick_max_ns`
- `tick_total_ns`

It also includes one line per touched node with:

- node id and node name
- `success`/`failure`/`running` return counts
- `last_ns`
- `max_ns`

## `bt.scheduler.stats` Output (What It Contains)

Current scheduler dump fields:

- `submitted`
- `started`
- `completed`
- `failed`
- `cancelled`
- `queue_delay_last_ns`
- `run_time_last_ns`

These are useful for a quick sanity check of async throughput and recent latency.

## Tick Budget Warnings

Set a tick budget:

```lisp
(bt.set-tick-budget-ms inst 20)
```

If a tick exceeds budget:

- profile overrun counters increment
- warning trace/log records are emitted

## GC/Heap Context For Performance

Use heap/GC counters to distinguish BT logic overhead from memory pressure.

```lisp
(heap-stats)
(gc-stats)
```

Printed counters include:

- total allocated objects
- live objects after last GC
- bytes allocated
- next GC threshold

## Practical Profiling Workflow

1. run with a realistic tick cadence
2. capture `bt.stats` and `bt.scheduler.stats`
3. if latency spikes appear, inspect trace/log around same tick window
4. confirm GC pressure with `heap-stats` / `gc-stats`
5. repeat in Release build for throughput numbers

## Debug vs Release

- Debug builds are preferred for semantic validation and instrumentation checks.
- Release builds are preferred for representative performance measurements.

## Known Boundaries

- percentile/histogram reports are not built in
- time-series export is not built in
- scheduler metrics are global in the default host

## See Also

- [Tracing](tracing.md)
- [Logging](logging.md)
- [Planner Logging Schema](planner-logging.md)
- [Roadmap](../limitations-roadmap.md)
