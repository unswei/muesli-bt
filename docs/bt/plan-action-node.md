# `plan-action` BT node

## what this is

`plan-action` is the BT leaf that runs a bounded planner call from inside a tick.

Status: released. Exact options and defaults are documented in the schema-backed [planning reference](../planning/plan-action-node.md).

## when to use it

Use `plan-action` when a tree needs a planner decision and the surrounding BT should decide what fallback branch runs on failure.

## how it works

The node reads planner state from the blackboard, calls `planner.plan`, writes the selected action to the blackboard, and returns `success` only for planner status `:ok`.

## api / syntax

```lisp
(plan-action key value key value ...)
```

The public option schema is:

```text
schemas/bt_node_options/v1/plan-action.schema.json
```

## example

```lisp
(sel
  (seq
    (cond bb-has state)
    (plan-action :planner :mcts :budget_ms 20 :state_key state :action_key action)
    (act apply-action action))
  (act safe-stop))
```

## gotchas

- Keep a later fallback branch for safe behaviour.
- Keep planner budgets below the robot loop budget.
- Use the planning reference for the complete option table.

## see also

- [planning `plan-action` reference](../planning/plan-action-node.md)
- [planner configuration](planner-configuration.md)
- [bounded-time planning](bounded-time-planning.md)
