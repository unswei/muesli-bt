# invocation-scoped authority

!!! note "status"

    Status: experimental.

    Invocation-scoped authority is implemented for the `vla-request`,
    `vla-wait`, and `vla-cancel` BT nodes. The default remains
    `deadline_only` for compatibility with existing trees.

## what this is

Invocation-scoped authority prevents an asynchronous result from being
committed after the request that created it has become obsolete.

Each `vla-request` creates a runtime record containing:

- backend job ID;
- generation;
- requesting node ID;
- current authority-owner node ID;
- blackboard job key;
- context key and captured context ID;
- monotonic submission time and deadline; and
- acceptance policy, authority state and terminal reason.

The immutable requesting node identifies where the invocation began. The
authority-owner node identifies the BT leaf that currently keeps it alive.

## when to use it

Use `invocation_scoped` when the world or BT branch can change while a model,
planner or remote service is running. Examples include:

- an observed object moving;
- a perception track being replaced;
- a newer request superseding an older request; or
- a safety branch pre-empting the branch that is waiting for a result.

Use `deadline_only` only when compatibility or a deliberate research baseline
requires the previous behaviour. A deadline limits elapsed time. It does not
show that the captured world context is still current.

## how it works

### submission

`vla-request` reads `:context_key` from the blackboard. The value must be a
non-empty string or an integer. The runtime stores the value as the captured
context ID.

The runtime increments a generation counter for `:job_key`. The first
generation is `1`. Submitting a new invocation for the same job key revokes and
removes the previous stored invocation before recording the new one.

The request node initially owns authority.

### request-to-wait hand-off

When `vla-wait` reads the matching job ID, it adopts authority before polling.
This hand-off matters for reactive selectors: selecting the higher-priority wait
branch may halt the earlier request branch, but that normal hand-off must not
revoke the job.

The requesting node remains unchanged for identity and evidence. Only the
authority-owner node changes.

### terminal latch

Each invocation emits at most one terminal `vla_result` decision. If
`:clear_job #f` retains the job ID, later `vla-wait` ticks return `success` for
an accepted invocation and `failure` for a rejected or revoked invocation.
They do not poll the backend, repeat host validation, rewrite the action or
emit another terminal decision. Clear the job key or submit a new generation
to begin a new lifecycle.

### commit gate

For `invocation_scoped`, a final or early result is committed only when:

```text
authority state is active
and the stored generation is current for the job key
and monotonic time is not after the stored deadline
and the current context ID equals the captured context ID
and the invocation has no terminal decision
and the action shape is valid
and the host commit validator accepts the proposal
```

The action blackboard key is written only after this gate passes. Acceptance
sets the authority state to `accepted`. A failed gate sets it to `rejected`,
unless it was already `revoked`.

Stable rejection reasons include:

- `branch_revoked`;
- `cancelled`;
- `context_changed`;
- `deadline_expired`;
- `duplicate_terminal_result`;
- `superseded`;
- `invalid_schema`;
- `invalid_frame`;
- `invalid_pose`;
- `ball_stale`;
- `robot_unstable`;
- `host_policy_rejected`; and
- `backend_terminal_failure`.

### host validation

The runtime checks that a BT VLA proposal is a finite continuous action with
the dimensions captured at submission. The host can then apply robot-specific
policy through `vla_commit_validator`.

For `invocation_scoped`, a missing host validator fails closed with
`host_policy_rejected`. The validator runs only after generation, authority,
context, deadline, exactly-once and structural checks pass. A terminal
invocation never calls the validator again.

The validator receives `vla_commit_context`, which contains the job ID,
generation, requesting and authority-owner nodes, job key, captured and current
context IDs, expected action frame, and whether the proposal is an early
result. It also receives the untrusted `vla_action` proposal, including any
frame reported by the backend.

The SDK-independent `approach_pose_validator` implements the common humanoid
checks for a three-component `[x_m, y_m, yaw_rad]` result: requested and reported
frame, pose bounds, current ball context and robot stability. A Booster adapter
still supplies the live state snapshot and applies any observation-age,
operating-area and walking-dispatch policy. See
[approach pose host validation](approach-pose-validation.md).

Returning an undocumented reason is normalised to `host_policy_rejected`.
Validator exceptions are also rejected with that reason.

### branch pre-emption

If a VLA authority-owner node is halted, the runtime sets an active invocation
to `revoked` with reason `branch_revoked`. It then requests best-effort backend
cancellation and removes the job from active runtime tracking. Halt cleanup
applies to both acceptance policies.

The invocation records the `vla-wait` action and metadata keys. Halt clears the
matching job key plus those result keys before another branch can use them.
Deletion is emitted through canonical `bb_delete` events. A job key is cleared
only when it still contains the invocation's job ID, so an old invocation
cannot erase a newer job.

Logical revocation happens before cancellation is requested. A backend refusing
or racing with cancellation cannot restore authority.

`vla-cancel` also revokes the stored invocation before requesting backend
cancellation and clearing the tracked job and result keys.

`bt.reset` and `runtime_host::reset_instance` halt the tree before clearing its
state. Active VLA work is therefore revoked and cancelled before invocation
records disappear. `runtime_host::clear_all` performs the same cleanup before
destroying instances.

### compatibility policy

`deadline_only` remains the default. It records the same invocation metadata,
but its commit gate checks only the stored deadline. It does not compare the
generation, context ID or terminal authority state.

This policy distinction supports controlled baseline experiments. It must not be
used to imply stale-result safety.

## api / syntax

Enable invocation-scoped authority on `vla-request`:

```lisp
(vla-request
  :name "ball-approach"
  :job_key approach-job
  :instruction "choose an approach pose"
  :state_key ball-state
  :dims 3
  :action_frame ball_context
  :deadline_ms 3500
  :acceptance_policy invocation_scoped
  :context_key ball-context-id)
```

New request options:

| option | type | default | meaning |
| --- | --- | --- | --- |
| `:acceptance_policy` | string or symbol | `deadline_only` | `deadline_only` or `invocation_scoped`. |
| `:context_key` | string or symbol | unset | Blackboard key holding a non-empty string or integer context ID. Required for `invocation_scoped`. |
| `:action_frame` | string or symbol | unset | Expected output action frame captured with the invocation. |

`vla-wait` and `vla-cancel` use the invocation associated with their configured
`:job_key`. They require no new options.

Register a host validator before ticking an invocation-scoped tree. For a
humanoid approach pose, use the concrete validator:

```cpp
bt::approach_pose_validator validator(
    bt::approach_pose_validator_config{
        .frame_id = "ball_context",
        .bounds = {
            .min_x_m = -1.0,
            .max_x_m = 0.0,
            .min_y_m = -0.5,
            .max_y_m = 0.5,
            .min_yaw_rad = -3.141593,
            .max_yaw_rad = 3.141593,
        },
    },
    [&robot_state] {
        return robot_state.approach_pose_host_state();
    });
host.set_vla_commit_validator(&validator);
```

The host retains ownership of the validator. The validator must outlive every
tick that can use it. Passing `nullptr` removes the callback.

Authority states are:

| state | meaning |
| --- | --- |
| `active` | The invocation may still pass its commit gate. |
| `revoked` | BT control flow or explicit cancellation removed authority. |
| `accepted` | A result passed the gate and consumed authority. |
| `rejected` | A returned result failed the gate or backend validation. |

## example

This shape keeps safety at the highest priority, lets `vla-wait` adopt the
invocation, and submits only when no current result can be consumed:

```lisp
(reactive-sel
  (seq
    (cond bb-truthy emergency)
    (act safe-stand))
  (seq
    (vla-wait
      :name "ball-approach"
      :job_key approach-job
      :action_key approach-pose)
    (act dispatch-validated-approach approach-pose))
  (seq
    (act safe-wait)
    (vla-request
      :name "ball-approach"
      :job_key approach-job
      :instruction "choose an approach pose"
      :state_key ball-state
      :dims 3
      :action_frame ball_context
      :deadline_ms 3500
      :acceptance_policy invocation_scoped
      :context_key ball-context-id)))
```

If `emergency` becomes true while the wait branch is running, the reactive
selector halts that branch. The runtime revokes its invocation and does not
allow the late action to update `approach-pose`.

If `ball-context-id` changes instead, the backend may continue running. The
commit gate rejects its result with `context_changed` when it arrives.

## gotchas

- The host must update the context ID before the BT tick that observes a moved
  or replaced object.
- Floating-point coordinates are not suitable context IDs. Use a perception
  lifecycle or generation identifier.
- Use the same `:job_key` on request, wait and cancel nodes.
- A request node and an authority-owner node are not always the same node.
- Cancellation is resource management. Revocation is the acceptance rule.
- Use the service-aware `reset(instance&, registry&, services&)` overload when
  embedding the runtime directly. The one-argument overload clears in-memory
  state only because it has no VLA service through which to request
  cancellation.
- `invocation_scoped` fails closed when the host has not registered a commit
  validator.
- `vla.submit` and `vla.poll` Lisp built-ins do not create BT invocation
  authority records. This feature belongs to the BT VLA nodes.
- Result acceptance does not replace host-side action validation.

## see also

- [VLA BT nodes](vla-nodes.md)
- [VLA integration](vla-integration.md)
- [approach pose host validation](approach-pose-validation.md)
- [VLA logging](../observability/vla-logging.md)
- [humanoid model-mediated approach experiment](../project/humanoid-model-mediated-approach-contract.md)
- [terminology](../terminology.md)
