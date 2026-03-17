# Logging

muesli-bt now uses one canonical event stream for logging, tracing, planner records, and VLA lifecycle events.

Use the event stream APIs:

- `(events.enable #t/#f)`
- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.set-ring-size n)`
- `(events.dump [n])`
- `(events.snapshot-bb [#t])`

For schema and event types, see [Canonical Event Log](event-log.md).
