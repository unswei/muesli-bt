# Terminology

This glossary defines the core terms used across the muesli-bt docs.

## host

The **host** is the embedding application that runs muesli-bt and connects it to the outside world. In practice this is usually your robotics process, simulator adapter, or control service that owns sensors, actuators, clocks, and lifecycle.

The host is responsible for wiring callbacks/services, deciding tick cadence, and deciding how safe fallback actions are applied when logic fails or times out.

## backend

A **backend** is the host-side implementation of the `env.*` capability surface for a specific platform (for example Webots, PyBullet, ROS2, or direct hardware). In docs, we often write "host (backend)" once and then use "backend" in user-facing text.

A backend translates platform-specific details into stable schemas and operations (`env.observe`, `env.act`, `env.step`, `env.run-loop`).

## capability

A **capability** is a host-provided function/module exposed to Lisp with controlled semantics. Capabilities let scripts call outside functionality without directly handling driver objects or middleware internals.

Examples: `env.*`, planner calls, and VLA calls.

## blackboard

The **blackboard** is the shared per-instance data store used by behaviour tree nodes and Lisp glue code.

Conditions and actions read/write blackboard keys each tick. This is where observations, planned actions, and metadata are passed between nodes.

## tick

A **tick** is one iteration of the behaviour tree execution cycle.

During a tick, the tree evaluates from root to leaves, returns `success`/`failure`/`running`, and may update blackboard values.

## budget

A **budget** is a bound on work per control cycle, usually time (`budget_ms`) and optionally work count (`work_max`).

Budgets are used both at BT tick level and planner level to keep control loops responsive.

## schema

A **schema** is a named, versioned data contract for observations, actions, or logs.

Examples: `planner.request.v1`, `planner.result.v1`, `planner.v1`, and backend-specific observation/action schema identifiers.

## BT language (DSL)

The **BT language** is muesli-bt's small, purpose-built language for writing behaviour trees. "DSL" means **domain-specific language**.

In practice, this is the Lisp-shaped BT syntax (`seq`, `sel`, `cond`, `act`, `plan-action`, ... ) that is compiled to runtime tree definitions.
