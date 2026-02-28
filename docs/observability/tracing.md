# Tracing And Debugging

Tracing provides structured event history per BT instance.

## Event Kinds

Core events implemented:

- `tick_begin`, `tick_end`
- `node_enter`, `node_exit`
- `bb_write`
- optional `bb_read`
- `scheduler_submit`, `scheduler_start`, `scheduler_finish`, `scheduler_cancel`
- `warning`, `error`

## Trace Buffer Model

- bounded in-memory ring-like buffer
- oldest events are evicted when capacity is reached
- per-instance trace storage

## Inspecting Trace

```lisp
(bt.trace.dump inst)
(bt.trace.snapshot inst)
```

Control switches:

```lisp
(bt.set-trace-enabled inst #t)
(bt.set-read-trace-enabled inst #t)
(bt.clear-trace inst)
```

## Example Trace Snippet

```text
11 kind=tick_begin tick=3 node=4 ts_ns=...
12 kind=node_enter tick=3 node=4 ts_ns=...
13 kind=bb_write tick=3 node=2 key=target-visible value=#t ts_ns=...
14 kind=node_exit tick=3 node=4 status=success duration_ns=...
15 kind=tick_end tick=3 node=4 status=success duration_ns=...
```

## Blackboard Trace Example (Key Changes Over Time)

```text
21 kind=bb_write tick=5 node=7 key=target-visible value=#f
33 kind=bb_write tick=8 node=7 key=target-visible value=#t
```

This makes it easy to confirm when a key changed. Writer details are available in `bt.blackboard.dump`.

## Debugging Workflows

## 1. `running` loop diagnosis

- inspect repeated `node_exit ... status=running`
- verify leaf progress in node memory / blackboard
- check if fallback branch is ever entered

## 2. stale blackboard values

- inspect `bt.blackboard.dump` metadata (`tick`, `writer`)
- correlate with `bb_write` events in trace

## 3. branch misfires

- confirm condition order via `node_enter`
- verify condition outcomes via `node_exit` statuses
- enable `bb_read` tracing temporarily if key reads are suspect

## See Also

- [Logging](logging.md)
- [Profiling](profiling.md)
- [Blackboard](../bt/blackboard.md)
