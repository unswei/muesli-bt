# `events.set-flush-each-message`

**Signature:** `(events.set-flush-each-message enabled?) -> nil`

Control whether the canonical event file sink flushes after every emitted message.

- `#f` keeps the file sink buffered by default
- `#t` flushes after every emitted canonical event line

Use this when you prefer durability during long-running or failure-prone runs over maximum logging throughput.
