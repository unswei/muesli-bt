# `plan-action` node

## what this is

`plan-action` is a Behaviour Tree (BT) leaf that calls `planner.plan` during a tick and writes the selected action to the blackboard.

Status: released. The BT status semantics are stable. The public option schema is `schemas/bt_node_options/v1/plan-action.schema.json`.

## when to use it

Use `plan-action` when task logic needs a bounded planner call and a normal BT fallback branch should handle planner failure.

Do not use it for hard real-time servo control. Keep low-level actuator safety in the host or backend.

## how it works

On each tick, `plan-action`:

1. reads state from `:state_key`;
2. builds a `planner.request.v1`;
3. calls the selected planner backend;
4. writes the result action to `:action_key`;
5. optionally writes compact metadata JSON to `:meta_key`;
6. returns `success` only when the planner result status is `:ok`.

If the planner times out, returns no action, returns invalid numeric values, or cannot read the state key, the node returns `failure`.

## api / syntax

```lisp
(plan-action key value key value ...)
```

Options are validated as the JSON-object shape described by `schemas/bt_node_options/v1/plan-action.schema.json`.

| option | type | default | notes |
| --- | --- | --- | --- |
| `:name` | string or symbol | `plan-action-<node_id>` | Node name used in planner records. |
| `:planner` | `:mcts`, `:mppi`, `:ilqr` | `:mcts` | Planner backend. |
| `:budget_ms` | integer > 0 | `20` | Per-call planner budget. |
| `:work_max` | integer >= 0 | `0` | Work cap. Alias: `:iters_max`. |
| `:horizon` | integer >= 0 | `0` | Planning horizon override. |
| `:dt_ms` | integer >= 0 | `0` | Planner step duration override. |
| `:model_service` | string or symbol | `"toy-1d"` | Planner model name. |
| `:state_key` | string or symbol | `state` | Blackboard key containing a number or numeric vector. |
| `:action_key` | string or symbol | `action` | Blackboard key that receives the selected action. |
| `:meta_key` | string or symbol | unset | Optional metadata output key. |
| `:safe_action` | number | unset | One-dimensional fallback action seed. Alias: `:fallback_action`. |
| `:safe_action_key` | string or symbol | unset | Reads fallback action vector from the blackboard. |
| `:seed_key` | string or symbol | unset | Reads deterministic seed from the blackboard. |
| `:action_schema` | string or symbol | `action.u.v1` when needed | Action schema id for output. |
| `:top_k` | integer >= 0 | `3` | Number of trace choices to record where supported. |

Planner-specific options are also schema-backed:

| backend | options |
| --- | --- |
| MCTS | `:gamma`, `:max_depth`, `:c_ucb`, `:pw_k`, `:pw_alpha`, `:rollout_policy`, `:action_sampler` |
| MPPI | `:lambda`, `:sigma`, `:sigma_key`, `:n_samples`, `:n_elite` |
| iLQR | `:max_iters`, `:reg_init`, `:reg_factor`, `:tol_cost`, `:tol_grad`, `:fd_eps`, `:derivatives` |
| constraints | `:max_du`, `:max_du_key`, `:smoothness_weight`, `:collision_weight`, `:goal_tolerance` |

## example

```lisp
(defbt guarded-plan
  (sel
    (seq
      (cond bb-has state)
      (plan-action
        :name "racecar-plan"
        :planner :mcts
        :budget_ms 20
        :work_max 1200
        :model_service "racecar-kinematic-v1"
        :state_key state
        :action_key action
        :meta_key plan-meta)
      (act apply-action action))
    (act safe-stop)))
```

The fallback branch is required for robot safety. A host should also keep its own safe action as the final guard.

## gotchas

- `:state_key` must exist before the node ticks.
- A non-`:ok` planner result is a BT `failure`, not a partial success.
- The node validates finite numeric action values before writing `:action_key`.
- The planner call is bounded, but it still runs in the tick path. Keep budgets small enough for the robot loop.

## see also

- [planner.plan request/result](planner-plan.md)
- [planning overview](overview.md)
- [BT semantics](../bt/semantics.md)
- [generated fragment validation](../tutorials/generated-guarded-recovery.md)
