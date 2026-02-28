# VLA Logging Schema

VLA jobs emit structured records to an in-memory ring and optional JSON lines file.

## Commands

- `(vla.logs.dump [n])`
- `(vla.set-log-path "path")`
- `(vla.set-log-enabled #t/#f)`
- `(vla.clear-logs)`

## Record Fields

Each record includes:

- `ts_ms`
- `run_id`
- `tick_index`
- `node_name`
- `task_id`
- `capability`
- `model_name`
- `model_version`
- `request_hash`
- `observation`
- `deadline_ms`
- `seed`
- `status`
- `latency_ms`
- `cache_hit`
- `replay_hit`
- `superseded`
- `response`

## Example Query

```lisp
(begin
  (vla.set-log-enabled #t)
  (vla.logs.dump 20))
```

## Notes

- records are bounded in memory
- JSON lines output is append-only when enabled
- response payload stays structured for replay and post-analysis

## See Also

- [Logging](logging.md)
- [VLA Integration In BTs](../bt/vla-integration.md)
- [Planner Logging Schema](planner-logging.md)
