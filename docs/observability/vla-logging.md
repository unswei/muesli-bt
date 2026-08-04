# VLA Logging

VLA lifecycle is emitted through canonical event types:

- `vla_submit`
- `vla_poll`
- `vla_cancel`
- `vla_result`
- `async_authority_revoked`

BT-node `vla_submit` and `vla_poll` events include invocation fields when a
runtime invocation record is available:

- `job_id`;
- `generation`;
- `requesting_node_id` and `authority_node_id`;
- `job_key`;
- `context_key`, `captured_context_id` and the current context on decisions;
- `submitted_at_ns` and `deadline_at_ns`;
- `acceptance_policy`; and
- `authority_state`.

BT-node `vla_result` events add `decision` (`accepted` or `rejected`) and a
stable `reason`. `async_authority_revoked` records logical revocation before
best-effort cancellation is requested.

Use the canonical event APIs:

- `(events.enable #t/#f)`
- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.dump [n])`

See [Canonical Event Log](event-log.md).
