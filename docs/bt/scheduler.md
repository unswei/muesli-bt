# Scheduler And Execution Model

This page describes how `bt.tick` and background scheduler work together in the current runtime.

## Overview

- [host](../terminology.md#host) code owns tick cadence and calls `(bt.tick inst)`
- async work can run on the background `thread_pool_scheduler`
- async outcomes are reconciled on later ticks
- scheduler activity is observable through stats and trace events

## Available Behaviour

- host-driven ticking via `bt.tick`
- background scheduler with worker threads
- scheduler job lifecycle tracking (`queued`, `running`, `done`, `failed`, `cancelled`)
- scheduler counters/timings via `bt.scheduler.stats`
- reference async leaf pattern via `async-sleep-ms`
- VLA async jobs (`vla.submit` / `vla.poll` / `vla.cancel`) and BT VLA nodes

## Lisp-Facing Scheduler Surfaces

| Surface | Purpose | Notes |
| --- | --- | --- |
| `(bt.scheduler.stats)` | View scheduler counters/timings | Global scheduler stats from runtime host |
| `(act async-sleep-ms <ms>)` | Demo async action pattern | Returns `running` until job completes |
| `(vla.submit req)` + `(vla.poll id)` | Capability-backed async policy calls | Non-blocking, scheduler-owned lifecycle |
| `(bt.tick inst)` | Advance async leaves | Polling/reconciliation happens on tick |
| `(bt.trace.snapshot inst)` | See scheduler events | Includes submit/start/finish/cancel events |

## Scheduler Trace Events

When tracing is enabled, scheduler activity appears as:

- `scheduler_submit`
- `scheduler_start`
- `scheduler_finish`
- `scheduler_cancel`

## C++ Scheduler API (Host/Leaf Side)

The scheduler interface exposes:

- `submit(job_request)`
- `get_info(job_id)`
- `try_get_result(job_id, out)`
- `cancel(job_id)`
- `stats_snapshot()`

Typical leaf pattern:

1. submit work once and store `job_id` in node memory
2. return `running`
3. poll `get_info` on later ticks
4. read result with `try_get_result` when `done`
5. return `success`/`failure`

## Threading Boundary

- Lisp evaluation and BT ticking are expected on one owning host thread.
- Scheduler jobs run on background worker threads.
- BT instance state (`node_memory`, blackboard, trace, per-node stats) is updated on tick path.
- Worker jobs should not mutate BT instance state directly.

Concurrent `bt.tick` on the same instance is not part of the supported model; treat each instance as single-owner during tick.

## `async-sleep-ms` Lifecycle (Reference Pattern)

1. first tick submits scheduler job and returns `running`
2. follow-up ticks observe queued/running status and keep returning `running`
3. completion tick consumes job result and returns `success`
4. failures/cancellations return `failure`

## Reset, Cancellation, And Halt

- `bt.reset` clears per-node memory and blackboard state for the instance.
- cancellation is best-effort in scheduler-backed actions.
- explicit leaf halt lifecycle is planned; see [Roadmap](../limitations-roadmap.md).

## Practical Guidance

- keep tick-thread leaf work short
- offload blocking I/O or long compute to scheduler jobs
- merge async outcomes in deterministic tick logic
- use trace plus scheduler stats when diagnosing latency

## See Also

- [BT Semantics](semantics.md)
- [Profiling](../observability/profiling.md)
- [Roadmap](../limitations-roadmap.md)
