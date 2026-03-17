# Planner Logging

Planner outputs are emitted through canonical event type `planner_v1`.

Payload shape:

- `data.record` contains the existing `planner.v1` object unchanged.

Use event stream controls for file/ring output:

- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.enable #t)`
- `(events.dump [n])`

See [Canonical Event Log](event-log.md).
