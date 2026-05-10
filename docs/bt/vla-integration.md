# VLA Integration In BTs

!!! note "status"
    Status: released lifecycle hooks, deterministic stubs, and an optional model-service session adapter.
    The runtime exposes submit, poll, cancel, timeout, BT node semantics, and canonical logging. Production provider transport and credentials remain outside the core runtime.

This runtime exposes VLA access through [host](../terminology.md#host) capabilities and async jobs.

Current repository scope: **stub integration + contract hooks**.

Implemented now:

- async submit/poll/cancel interface and BT node semantics
- cancellation and timeout behaviour in runtime lifecycle
- canonical `mbt.evt.v1` logging for VLA events

Host-defined / optional in this repo:

- production model/provider transport and credentials
- remote inference hardening beyond stub/replay adapters
- `muesli-model-service` bridge transport and VLA session adapter, when the optional bridge is built and configured

Key principles:

- mueslisp does not perform raw HTTP/model credential handling
- BT ticks do not block on VLA calls
- jobs are submitted, polled, and cancelled through scheduler-owned state
- responses are validated before actions are written to blackboard

## Capability Boundary

Capability metadata is host-registered and inspectable from Lisp:

- `(cap.list)`
- `(cap.describe "vla.rt2")`

A request is submitted with `(vla.submit request-map)`. The runtime executes it behind a registered backend (stub, replay, model-service, or custom host backend).

The first external model-service bridge keeps the same lifecycle surface but uses stable capability ids such as `cap.vla.action_chunk.v1`. Select it by configuring the optional model-service bridge and setting the VLA request model name to `model-service`. See [muesli-model-service bridge](../integration/model-service-bridge.md).

## Async Lifecycle

`vla.poll` reports one of:

- `:queued`
- `:running`
- `:streaming`
- `:done`
- `:error`
- `:timeout`
- `:cancelled`

`vla.cancel` is best-effort and marks the job cancelled when possible.

## BT Pattern

Typical non-blocking tree shape:

```lisp
(sel
  (seq
    (vla-wait :name "policy" :job_key policy-job :action_key policy-action)
    (act apply-planned-1d state policy-action state)
    (succeed))
  (seq
    (act bb-put-float fallback-action 0.0)
    (vla-request :name "policy" :job_key policy-job :instruction_key instruction :state_key state)
    (running)))
```

The selector lets `vla-wait` complete when ready, while fallback logic continues during waiting ticks.

## See Also

- [VLA BT Nodes](vla-nodes.md)
- [VLA Request/Response Schema](vla-request-response.md)
- [VLA Logging Schema](../observability/vla-logging.md)
- [muesli-model-service Bridge](../integration/model-service-bridge.md)
- [Bounded-Time Planning](bounded-time-planning.md)
