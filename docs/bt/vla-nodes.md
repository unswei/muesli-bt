# VLA BT nodes

## what this is

The VLA BT nodes submit, poll, and cancel asynchronous Vision-Language-Action (VLA) jobs through the host VLA service.

Status: released for lifecycle semantics and deterministic stubs. Model-service backed sessions remain experimental unless a release note lists a concrete backend as supported.

The public option schemas are:

```text
schemas/bt_node_options/v1/vla-request.schema.json
schemas/bt_node_options/v1/vla-wait.schema.json
schemas/bt_node_options/v1/vla-cancel.schema.json
```

## when to use it

Use these nodes when a BT should request an action proposal from an asynchronous model or VLA service, then fall back safely if the result is late, invalid, cancelled, or unavailable.

Do not send model output directly to actuators. The host must validate proposals before physical dispatch.

## how it works

`vla-request` creates a job and stores its id in the blackboard. `vla-wait` polls that id and writes a valid action to the blackboard. `vla-cancel` cancels and clears the idempotent job key.

When `vla-wait` adopts a job, the invocation also records its configured
`:action_key` and `:meta_key`. Subtree halt, explicit cancellation and
service-aware reset clear the tracked job and result keys.

`vla-request` also creates an invocation record. Existing trees use the
`deadline_only` acceptance policy by default. Set `:acceptance_policy
invocation_scoped` and provide `:context_key` when a result must remain bound to
the current BT authority, request generation and world context. The host must
also register a `vla_commit_validator`; full mode fails closed without one. See
[invocation-scoped authority](invocation-scoped-authority.md).

The usual pattern is:

```lisp
(reactive-sel
  (seq
    (vla-request :name "policy" :instruction "move to target" :state_key state)
    (vla-wait :name "policy" :action_key action)
    (act apply-action action))
  (act safe-stop))
```

The fallback branch is part of the safety contract.

## api / syntax

### `vla-request`

```lisp
(vla-request key value key value ...)
```

| option | type | default | notes |
| --- | --- | --- | --- |
| `:name` | string or symbol | `vla-request-<node_id>` | Node and job-key base name. |
| `:job_key` | string or symbol | `<name>.job_id` | Blackboard key for the async job id. |
| `:instruction` | string or symbol | unset | Inline instruction. |
| `:instruction_key` | string or symbol | `instruction` | Blackboard key used when `:instruction` is unset. |
| `:task_id` | string or symbol | `task` | Task identifier. |
| `:task_key` | string or symbol | unset | Optional blackboard task id. |
| `:state_key` | string or symbol | `state` | Numeric state vector key. |
| `:image_key`, `:blob_key` | string or symbol | unset | Optional media handle keys. |
| `:capability` | string or symbol | `vla.rt2` | Host capability id. |
| `:model_name` | string or symbol | `rt2-stub` | Model name for records. |
| `:model_version` | string or symbol | `stub-1` | Model version for records. |
| `:frame_id` | string or symbol | `base` | Observation frame id. |
| `:action_frame` | string or symbol | unset | Expected output action frame. The backend result reports its frame separately. |
| `:deadline_ms` | integer > 0 | `20` | Async job deadline. Alias: `:budget_ms`. |
| `:acceptance_policy` | string or symbol | `deadline_only` | Use `invocation_scoped` to require current generation, active authority and matching context at commit. |
| `:context_key` | string or symbol | unset | Context ID key. Required by `invocation_scoped`; the value must be a non-empty string or integer. |
| `:dims` | integer >= 0 | state dimension | Action dimensions. |
| `:bound_lo`, `:bound_hi` | number | `-1.0`, `1.0` | Continuous action bounds. |
| `:max_abs`, `:max_delta` | number | `1.0`, `1.0` | Validation clamps. |
| `:forbidden_lo`, `:forbidden_hi` | number | unset | Optional forbidden scalar interval. |
| `:seed`, `:seed_key` | number/string or key | derived | Deterministic seed control. |

Return status:

- `running` after submit;
- `running` when the job key already holds an in-flight id;
- `failure` for missing state, missing instruction, invalid media handles, invalid bounds, invalid deadline, invalid or missing required context, or missing service.

### `vla-wait`

```lisp
(vla-wait key value key value ...)
```

| option | type | default | notes |
| --- | --- | --- | --- |
| `:name` | string or symbol | `vla-request-<node_id>` | Used to derive the default job key. |
| `:job_key` | string or symbol | `<name>.job_id` | Blackboard key containing the job id. |
| `:action_key` | string or symbol | `action` | Output key for the accepted action. |
| `:meta_key` | string or symbol | unset | Optional JSON poll summary. |
| `:early_commit` | boolean | `false` | Allows a streaming partial action to succeed early. |
| `:early_confidence` | number | `1.1` | Disabled by default because confidence cannot reach 1.1. |
| `:cancel_on_early_commit` | boolean | `true` | Cancels the remaining job after early commit. |
| `:clear_job` | boolean | `true` | Clears `:job_key` on terminal result. |

Return status:

- `success` when a valid final or early action is written;
- `running` while the job is queued, running, or streaming;
- `failure` on timeout, error, cancellation, invalid action, missing job key, or missing service.

### `vla-cancel`

```lisp
(vla-cancel key value key value ...)
```

| option | type | default | notes |
| --- | --- | --- | --- |
| `:name` | string or symbol | `vla-request-<node_id>` | Used to derive the default job key. |
| `:job_key` | string or symbol | `<name>.job_id` | Blackboard key containing the job id. |

Return status:

- `success` when a job was cancelled;
- `success` when there was nothing to cancel;
- `failure` only when the VLA service is not available.

## example

```lisp
(defbt guarded-vla
  (reactive-sel
    (seq
      (cond bb-has state)
      (vla-request
        :name "nav-policy"
        :instruction "move towards the goal"
        :state_key state
        :deadline_ms 50
        :capability "vla.rt2")
      (vla-wait
        :name "nav-policy"
        :action_key action
        :meta_key vla-meta)
      (act apply-action action))
    (act safe-stop)))
```

## gotchas

- Use the same `:name` or `:job_key` across request, wait, and cancel nodes.
- Use `invocation_scoped` when object identity or BT branch authority can change
  while the job is running.
- Register a host commit validator before ticking an invocation-scoped tree.
- Treat halt and reset as terminal cleanup: tracked VLA jobs are revoked,
  cancelled and removed from the blackboard.
- Keep a fallback branch after VLA work.
- Treat model output as a proposal until the host validates it.
- Prefer media handles such as `frame://camera1/latest` for remote calls instead of embedding image bytes in Lisp.

## see also

- [VLA integration](vla-integration.md)
- [invocation-scoped authority](invocation-scoped-authority.md)
- [VLA request/response](vla-request-response.md)
- [VLA logging](../observability/vla-logging.md)
- [generated guarded recovery](../tutorials/generated-guarded-recovery.md)
