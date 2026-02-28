# VLA Integration In BTs

This runtime exposes VLA access through host capabilities and async jobs.

Key principles:

- mueslisp does not perform raw HTTP/model credential handling
- BT ticks do not block on VLA calls
- jobs are submitted, polled, and cancelled through scheduler-owned state
- responses are validated before actions are written to blackboard

## Capability Boundary

Capability metadata is host-registered and inspectable from Lisp:

- `(cap.list)`
- `(cap.describe "vla.rt2")`

A request is submitted with `(vla.submit request-map)`. The runtime executes it behind a registered backend (stub, replay, or custom host backend).

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
- [Bounded-Time Planning](bounded-time-planning.md)
