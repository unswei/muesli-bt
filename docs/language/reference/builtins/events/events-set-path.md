# `events.set-path`

**Signature:** `(events.set-path path-string) -> nil`

Set JSONL output path for canonical events and enable file sink output.

The file sink is buffered by default. Use [`events.set-flush-each-message`](events-set-flush-each-message.md)
when you want a flush after every emitted canonical line.
