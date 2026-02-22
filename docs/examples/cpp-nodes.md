# Example: C++ Nodes And Debug Workflow

## Condition Callback Example

```cpp
host.callbacks().register_condition("target-visible",
    [](bt::tick_context& ctx, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) return false;
        return ctx.svc.robot->target_visible(ctx);
    });
```

## Action Callback With `running`

```cpp
host.callbacks().register_action("approach-target",
    [](bt::tick_context& ctx, bt::node_id, bt::node_memory& mem,
       std::span<const muslisp::value>) {
        if (!ctx.svc.robot) return bt::status::failure;
        return ctx.svc.robot->approach_target(ctx, mem);
    });
```

## Blackboard Write Example

```cpp
ctx.bb_put("target-visible", bt::bb_value{true}, "search-target");
```

## Trace + Profiling Workflow

1. enable trace

```lisp
(bt.set-trace-enabled inst #t)
```

2. tick and inspect

```lisp
(bt.tick inst)
(bt.trace.snapshot inst)
(bt.stats inst)
```

3. diagnose behaviour

- if branch selection is wrong, inspect `node_enter` order and condition outcomes
- if task stalls, inspect repeated `running` statuses and blackboard freshness (`tick`/`writer` metadata)
