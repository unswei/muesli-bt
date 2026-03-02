# Canonical Event Log (`mbt.evt.v1`)

`muesli-bt` emits one canonical event stream. Each line is JSON (`.jsonl`) with envelope:

```json
{
  "schema": "mbt.evt.v1",
  "type": "tick_begin",
  "run_id": "run-1735689600000",
  "unix_ms": 1735689600123,
  "seq": 42,
  "tick": 7,
  "data": {}
}
```

## Envelope fields

- `schema`: always `"mbt.evt.v1"`
- `type`: event type
- `run_id`: run identifier
- `unix_ms`: UTC milliseconds
- `seq`: strictly increasing per run
- `tick`: optional tick index
- `data`: type-specific payload

## Event types

- `run_start`
- `bt_def`
- `tick_begin`
- `tick_end`
- `node_status`
- `bb_write`
- `bb_delete`
- `bb_snapshot`
- `sched_submit`, `sched_start`, `sched_finish`, `sched_cancel`
- `planner_v1`
- `vla_submit`, `vla_poll`, `vla_cancel`, `vla_result`
- `error`

## Lisp controls

- `(events.enable #t/#f)`
- `(events.set-path "logs/run.jsonl")`
- `(events.set-ring-size n)`
- `(events.dump [n])` -> list of JSON strings
- `(events.snapshot-bb [#t])` -> request snapshot at next tick boundary

## Notes

- Blackboard `bb_write.preview` is size-limited (4KB JSON).
- `seq` is the authoritative ordering key for replay/monitoring.
- Existing planner/vla metadata is wrapped in canonical events (for example `planner_v1`).
