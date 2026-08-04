# VLA Logging

VLA lifecycle is emitted through canonical event types:

- `vla_submit`
- `vla_poll`
- `vla_cancel`
- `vla_result`
- `async_authority_revoked`
- `walking_target_dispatch`

BT-node `vla_submit` and `vla_poll` events include invocation fields when a
runtime invocation record is available:

- `job_id`;
- `generation`;
- `requesting_node_id` and `authority_node_id`;
- `job_key`;
- `action_key` and `meta_key` when a wait node has adopted the invocation;
- `context_key`, `captured_context_id` and the current context on decisions;
- `action_frame` when the request declares an expected output frame;
- `submitted_at_ns` and `deadline_at_ns`;
- `acceptance_policy`; and
- `authority_state`.

BT-node `vla_result` events add `decision` (`accepted` or `rejected`), a stable
`reason`, and:

- `host_validation`: `not_run`, `accepted` or `rejected`;
- `host_validation_reason`; and
- `host_validation_source`: `none`, `runtime_structural`, `vla_service`,
  `host_callback` or `unavailable`.

Authority failures short-circuit before host policy and use `not_run`.
Structural or host-policy rejection also emits the existing
`host_action_invalid` runtime outcome. `async_authority_revoked` records logical
revocation before best-effort cancellation is requested. Pre-emption and reset
then emit `bb_delete` for each tracked job or result key that still has a value.

Invocation-scoped `vla_submit` and `vla_result` payloads are schema-checked for
their correlation fields. Result payloads require generation, captured/current
context, authority state, terminal decision and reason. The
`async_authority_revoked` payload requires the generation, captured context,
revoked state and pre-emption reason.

`walking_target_dispatch` is the separate host hand-off record. Its payload
contains:

- job ID, generation and requesting, authority-owner and dispatching nodes;
- job, action and context keys;
- captured and dispatch-time current context IDs;
- action frame and exact planar target `[x_m, y_m, yaw_rad]`;
- target digest;
- authority state;
- `decision` (`accepted` or `rejected`) and stable `reason`; and
- `dispatch_source` (`runtime_structural`, `host_callback` or `unavailable`).

An accepted dispatch event is emitted only after the registered host callback
reports that the walking-controller boundary accepted the target. Rejected or
revoked VLA results do not produce accepted dispatch events. A second dispatch
for the same invocation is rejected with `duplicate_dispatch`.

Use the canonical event APIs:

- `(events.enable #t/#f)`
- `(events.set-path "logs/run.jsonl")`
- `(events.set-flush-each-message #t/#f)`
- `(events.dump [n])`

See [Canonical Event Log](event-log.md).
