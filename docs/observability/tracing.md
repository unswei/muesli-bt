# Tracing And Debugging

Tracing is emitted through the canonical `mbt.evt.v1` event stream.

Key trace-equivalent event types:

- `tick_begin`, `tick_end`
- `node_status`
- `bb_write`, `bb_delete`, `bb_snapshot`
- `sched_submit`, `sched_start`, `sched_finish`, `sched_cancel`
- `error`

Use `(events.dump [n])` for recent events and `(events.snapshot-bb)` to request a snapshot at the next safe tick boundary.

See [Canonical Event Log](event-log.md) for envelope and payload details.
