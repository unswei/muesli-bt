# Connecting BT Nodes To Robot Functionality

BTs coordinate behaviour. Actual robot work usually happens in host-side C++ callbacks.

## Registry API

Conditions and actions are registered in `bt::registry`.

Conceptual signatures:

```cpp
using condition_fn = std::function<bool(tick_context&, std::span<const muslisp::value>)>;
using action_fn = std::function<status(tick_context&, node_id, node_memory&, std::span<const muslisp::value>)>;
```

## Typed Host Services

`bt::services` currently supports typed integration points:

- `scheduler*`
- observability (`trace_buffer*`, `log_sink*`)
- `clock_interface*`
- `robot_interface*`
- `planner_service*`
- `vla_service*`

`runtime_host` provides defaults and allows custom robot injection.

## Planner Service Integration

Host-side planning models are registered with `planner_service` and selected by name from `plan-action` and `planner.plan`.

Built-in model names:

- `"toy-1d"`
- `"ptz-track"`

Custom model registration uses:

```cpp
host.planner_ref().register_model("my-model", my_model_ptr);
```

## VLA Service Integration

VLA integration is capability-based and async:

- capabilities are introspectable (`cap.list`, `cap.describe`)
- requests are submitted/polled/cancelled (`vla.submit`, `vla.poll`, `vla.cancel`)
- image/blob observations are passed as handles (`image_handle`, `blob_handle`)

Custom backends are registered host-side through `vla_service`.

## Condition Example (`target-visible`)

```cpp
reg.register_condition("target-visible", [](bt::tick_context& ctx, std::span<const muslisp::value>) {
    if (!ctx.svc.robot) return false;
    return ctx.svc.robot->target_visible(ctx);
});
```

## Action Example Returning `running`

```cpp
reg.register_action("approach-target", [](bt::tick_context& ctx, bt::node_id, bt::node_memory& mem,
                                           std::span<const muslisp::value>) {
    if (!ctx.svc.robot) return bt::status::failure;
    return ctx.svc.robot->approach_target(ctx, mem);
});
```

A typical `approach_target` wrapper stores progress in `node_memory` and returns `running` until complete.

## Blackboard Read/Write In Leaves

```cpp
reg.register_action("set-goal", [](bt::tick_context& ctx, bt::node_id, bt::node_memory&,
                                    std::span<const muslisp::value>) {
    ctx.bb_put("goal-x", bt::bb_value{10}, "set-goal");
    return bt::status::success;
});
```

## Error Handling Guidance

- treat leaf boundaries as fault-isolation boundaries
- throw only for truly exceptional cases
- runtime converts thrown callback exceptions to `failure` plus trace/log error events

## Lifecycle Expectations

- conditions should be fast and side-effect light
- actions may span ticks using `running`
- reset must not leave leaf state inconsistent
- avoid direct long blocking on tick thread

## Practical Integration Model

BT layer decides *what should happen next*.

Host robotics code decides *how the robot actually executes it* (controllers, planners, ROS2 actions, drivers, etc.).

## Shipping Trees With `load`

For deployment scripts, keep BT and glue code in Lisp source files and load them at runtime:

```lisp
(load "robot_trees/patrol.lisp")
(define inst (bt.new-instance patrol-tree))
(bt.tick inst)
```

Recommended pattern:

- define trees with `defbt`
- keep callback names stable with host registry names
- use `bt.save-dsl` for portable artefacts when you need to export/import trees

## See Also

- [Scheduler](scheduler.md)
- [Bounded-Time Planning In BTs](bounded-time-planning.md)
- [VLA Integration In BTs](vla-integration.md)
- [BT Semantics](semantics.md)
- [Roadmap](../limitations-roadmap.md)
