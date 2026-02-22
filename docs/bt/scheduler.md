# Scheduler And Execution Model

This page documents exactly what scheduler/tick behaviour is implemented in v1, and what is still intentionally missing.

## Current Implementation State

Implemented now:

- Host-driven ticking via `bt.tick`.
- A real background scheduler (`thread_pool_scheduler`) with worker threads.
- Scheduler job lifecycle tracking (`queued`, `running`, `done`, `failed`, `cancelled`).
- Scheduler profiling counters surfaced via `bt.scheduler.stats`.
- A concrete scheduler-backed action example: `async-sleep-ms`.

Not implemented yet:

- no direct Lisp API to submit/cancel arbitrary scheduler jobs
- no explicit BT `halt` contract in v1
- no scheduler timeout enforcement in the default thread pool

## Who Owns Tick Cadence

`bt.tick` is called by host code (REPL loop, script loop, application supervisor, robot control loop). The runtime defines per-tick semantics, but the host owns timing and cadence.

## Lisp-Facing Scheduler Surfaces

### Commands Available Now

| Surface | Purpose | Notes |
| --- | --- | --- |
| `(bt.scheduler.stats)` | View scheduler counters/timings | Global scheduler stats from the runtime host |
| `(act async-sleep-ms <ms>)` | Demo async action pattern | Returns `running` until job completes |
| `(bt.tick inst)` | Advance async leaves | Polling/reconciliation happens on tick |
| `(bt.trace.snapshot inst)` | See scheduler events | Includes submit/start/finish/cancel events |

### Scheduler-Related Trace Directives

When tracing is enabled, scheduler activity appears as:

- `scheduler_submit`
- `scheduler_start`
- `scheduler_finish`
- `scheduler_cancel`

These are useful for confirming async progression across ticks.

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

## Threading Model (Current Boundary)

Current runtime model is mixed and explicit:

- Lisp evaluation and BT ticking are expected on an owning host thread.
- Scheduler jobs run on background worker threads.
- BT instance state (`node_memory`, blackboard, trace, per-node stats) is mutated on the tick path.
- Worker jobs should not mutate BT instance state directly.
- Async outcomes should be merged from the tick path on later ticks.

There is no v1 guarantee for concurrent `bt.tick` on the same instance. Treat instance ticking as single-owner.

## `async-sleep-ms` Lifecycle (Reference Example)

`async-sleep-ms` demonstrates the expected async contract:

1. first tick submits scheduler job and returns `running`
2. follow-up ticks observe queued/running status and keep returning `running`
3. completion tick consumes job result and returns `success`
4. failures/cancellations return `failure`

This is an action-level pattern, not a separate BT scheduler node type.

## Reset, Cancellation, And Halt

`bt.reset` clears per-node memory and blackboard, so in-flight async leaf progress tracked in node memory is discarded.

Cancellation in v1 is best-effort where used by actions/scheduler.

Explicit `halt` semantics are intentionally deferred to v2.

## Practical Guidance

- Keep tick-thread leaf work short.
- Offload blocking I/O or long computation to scheduler jobs.
- Reconcile async results in deterministic tick logic.
- Use trace plus scheduler stats together when diagnosing latency.
