# Logging

muesli-bt now uses one canonical event stream for logging, tracing, planner records, and VLA lifecycle events.

Use this page when you want a practical first debug loop.
Use [Canonical Event Log](event-log.md) when you need the authoritative event types, envelope fields, and validation details.

Use the event stream APIs:

- `(events.enable #t/#f)`
- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.set-ring-size n)`
- `(events.dump [n])`
- `(events.snapshot-bb [#t])`

## Quick Debug Workflow

1. enable or confirm event capture, then set a file path if you want a persistent log
2. run a small number of ticks
3. inspect recent events with `events.dump`
4. request a blackboard snapshot, then tick once more
5. validate the written log if you are debugging a longer or shared run

Example:

```lisp
(events.enable #t)
(events.set-path "logs/run.mbt.evt.v1.jsonl")
(bt.tick inst)
(events.dump 20)
(events.snapshot-bb)
(bt.tick inst)
```

`events.snapshot-bb` requests a snapshot at the next safe tick boundary.
That means you normally call it before the tick whose blackboard state you want to inspect.

## First Five Things To Inspect

- `tick_begin` and `tick_end`: confirms that the loop is actually ticking
- `error`: shows runtime or callback failures first
- `node_status`: shows which branch or leaf is returning `running`, `failure`, or `success`
- `bb_write` and `bb_snapshot`: shows whether the state you expect is actually reaching the blackboard
- scheduler or planner events such as `sched_*` and `planner_*`: useful when the tree waits on async work or bounded-time planning

## Validate A Written Log

Once you have written a file with `events.set-path`, validate it with:

```bash
python3 tools/validate_log.py logs/run.mbt.evt.v1.jsonl
```

For deeper trace-order checks and the full event reference, continue to [Canonical Event Log](event-log.md) and [Tracing And Debugging](tracing.md).
