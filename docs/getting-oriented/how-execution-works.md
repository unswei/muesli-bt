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

## From Observation To Action

The missing bridge for most newcomers is the data path between sensors, blackboard state, and host-side actions.

A typical run looks like this:

1. the backend gathers raw state and returns an observation map from `env.observe`
2. a small mapping step keeps only the stable fields the tree actually needs
3. those fields become blackboard keys, either through explicit writes or tick input
4. BT leaves read those keys and return `success`, `failure`, or `running`
5. host-side action callbacks or selected action outputs are then applied through `env.act`

Mini worked example:

- `env.observe` returns a map containing fields such as target visibility and goal position
- the runtime or glue code writes stable keys such as `target-visible` and `goal-x` into the per-instance blackboard
- `(cond target-visible)` reads that state and decides whether the guarded branch may continue
- `(act approach-target)` calls a host action callback, which may return `running` across several ticks while the host keeps track of progress
- once the chosen action is known, the backend applies it with `env.act` and advances the world with `env.step`

Callback leaves are therefore the boundary between BT structure and host implementation.
The tree decides what should run next; the host callback decides how that condition check or action is carried out on the actual platform.

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

## Manual Loop Versus Managed Loop

Prefer a backend-owned loop when you already have simulator timing, robot middleware timing, or a control loop that must remain the source of truth.
Prefer `env.run-loop` when you want a standard managed loop quickly, including the runtime's built-in deadline and fallback handling.
The BT semantics stay the same in both cases; the difference is who owns pacing and integration boundaries.

## See Also

- [Terminology](../terminology.md)
- [Integration Overview](../integration/overview.md)
- [Environment API (`env.*`)](../integration/env-api.md)
- [Sensing And Blackboard](../integration/sensing-and-blackboard.md)
- [Blackboard Design And Usage](../bt/blackboard.md)
- [Connecting BT Nodes To Robot Functionality](../bt/robot-integration.md)
- [Planning Overview](../planning/overview.md)
