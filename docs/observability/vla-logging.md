# VLA Logging

VLA lifecycle is emitted through canonical event types:

- `vla_submit`
- `vla_poll`
- `vla_cancel`
- `vla_result`

Use the canonical event APIs:

- `(events.enable #t/#f)`
- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.dump [n])`

See [Canonical Event Log](event-log.md).
