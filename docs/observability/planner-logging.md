# Planner Logging

Planner outputs are emitted through canonical event type `planner_v1`.

Payload shape:

- `data.record` contains the existing `planner.v1` object unchanged.

The `planner.v1` record contains:

- `schema_version`: `"planner.v1"`
- `run_id`, `tick_index`, `node_name`
- `planner`: `"mcts"`, `"mppi"`, or `"ilqr"`
- `status`: `"ok"`, `"timeout"`, `"noaction"`, or `"error"`
- `budget_ms`, `time_used_ms`, `work_done`, `seed`
- `action`
- `confidence`
- optional `overrun`, `note`, and `state_key`
- backend trace fields where available

`budget_ms` is a target checked at planner decision points.
If elapsed time exceeds the target, `overrun` is recorded.
Timeout and error results still include the resolved safe action, so downstream logic has an explicit fallback payload.

Use event stream controls for file/ring output:

- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.enable #t)`
- `(events.dump [n])`

See [Canonical Event Log](event-log.md).
