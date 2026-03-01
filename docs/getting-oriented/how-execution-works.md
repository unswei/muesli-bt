# How Execution Works

This page explains the control loop model used by muesli-bt.

## What A Tick Is

A tick is one evaluation pass of a behaviour tree instance. Each tick returns one status:

- `success`
- `failure`
- `running`

`running` means "continue next tick".

## Where Lisp Fits

Lisp code defines BT structure and helper logic. The [host](../terminology.md#host) (backend) supplies real-world functionality through capabilities (`env.*`, planner service, VLA service, callback leaves).

Lisp does not replace sensor drivers and motor controllers; it orchestrates them.

## Who Owns The Main Loop

You have two common loop ownership models:

1. backend-owned loop (most common): backend controls timing and repeatedly calls observe/tick/act/step
2. `env.run-loop` convenience path: runtime executes a managed loop with deadlines and fallback semantics

Both models follow the same execution shape.

## Minimal Loop Pseudocode

```text
while running:
  obs = env.observe()
  blackboard.write("obs", obs)

  status = bt.tick(instance)

  action = blackboard.read_or("action", safe_action)
  env.act(action)
  env.step()
```

## Budgets, Deadlines, And Fallback

- Tick budget: keeps each control cycle bounded.
- Planner budget: `plan-action` can enforce `budget_ms` plus `work_max`.
- Deadline miss / planner failure: backend should apply a safe fallback action.
- Invalid action output: backend should reject/clamp and fall back safely.

## Error Handling Model

At runtime, errors should degrade to safe behaviour:

- planner timeout/error/noaction -> BT branch can fail -> fallback branch can run
- missing backend attachment -> explicit error status/log
- malformed observations/actions -> schema validation and fallback action

## See Also

- [Terminology](../terminology.md)
- [Integration Overview](../integration/overview.md)
- [Environment API (`env.*`)](../integration/env-api.md)
- [Planning Overview](../planning/overview.md)
