# Behaviour Tree Semantics (Authoritative)

This page defines runtime semantics as implemented.

## Tick Semantics

`bt.tick` evaluates one instance from its root and returns `success`, `failure`, or `running`.

Per tick:

- root node is entered
- node statuses propagate upward
- trace/log/profiling hooks are updated when enabled
- tick returns immediately once root status is decided

`running` means work is incomplete and another tick is required.

## Definition vs Instance State

- definition (`bt_def`): immutable compiled node graph
- instance (`bt_instance`): mutable runtime state

Instance state includes:

- per-node memory map
- blackboard
- tick index
- trace buffer
- profiling counters

One definition can be shared across many instances.

## Composite Semantics

## Memoryless Composites

## `seq` (memoryless)

Evaluation order: left to right.

Rules:

- first child `failure` -> `failure`
- first child `running` -> `running`
- all children `success` -> `success`

Memoryless means it starts from child 0 on each tick.

## `sel` (memoryless)

Evaluation order: left to right.

Rules:

- first child `success` -> `success`
- first child `running` -> `running`
- all children `failure` -> `failure`

Memoryless means it starts from child 0 on each tick.

## Memoryful Composites

## `mem-seq` (sequence with memory)

`mem-seq` resumes from the stored child index instead of restarting from child 0.

Rules:

- start from stored `child_index` (default 0)
- if current child returns `success`, advance to next child in the same tick
- if last child succeeds, reset `child_index` to 0 and return `success`
- if current child returns `running`, keep `child_index` and return `running`
- if current child returns `failure`, keep `child_index` and return `failure`

This avoids redoing previously successful earlier steps.

## `mem-sel` (fallback with memory)

`mem-sel` resumes from the stored child index while a lower-priority child is running.

Rules:

- start from stored `child_index` (default 0)
- evaluate children left-to-right from that index
- on child `success`: reset index to 0 and return `success`
- on child `running`: store index and return `running`
- on child `failure`: continue to next child in the same tick
- if all children fail: reset index to 0 and return `failure`

This avoids retrying earlier failed children while a later child is still running.

## Yielding And Reactive Composites

## `async-seq` (yielding sequence)

`async-seq` intentionally yields between successful child boundaries.

Rules:

- tick current `child_index` (default 0)
- child `success` on non-last child: advance index and return `running`
- child `success` on last child: reset index to 0 and return `success`
- child `running`: return `running`
- child `failure`: reset index to 0 and return `failure`

Use this under reactive parents when you want frequent guard re-check points.

## `reactive-seq` (reactive sequence)

`reactive-seq` always re-evaluates from child 0 every tick.

Rules:

- evaluate left-to-right from child 0 every tick
- on first `failure`: return `failure`
- on first `running`: return `running`
- all children `success`: return `success`
- if a different subtree was running in the prior tick and is no longer selected, the runtime calls `halt_subtree` best-effort

## `reactive-sel` (reactive fallback)

`reactive-sel` also restarts from child 0 every tick, preserving priority ordering.

Rules:

- evaluate children left-to-right every tick
- first child returning `success` or `running` is returned immediately
- if all children fail: return `failure`
- if a lower-priority subtree was running in the prior tick and a higher-priority branch is now selected, pre-empt and halt the old running subtree best-effort

## Decorator Semantics

## `invert`

- `success -> failure`
- `failure -> success`
- `running -> running`

## `repeat n`

Uses per-node counter memory.

- if counter already `>= n`, returns `success`
- tick child otherwise
- child `failure` -> `failure`
- child `running` -> `running`
- child `success` increments counter
  - if counter reaches `n`, returns `success`
  - otherwise returns `running`

`n == 0` returns `success` immediately.

## `retry n`

Uses per-node retry counter.

- tick child
- child `success` -> resets counter to 0, returns `success`
- child `running` -> `running`
- child `failure` increments counter
  - if counter `<= n`, returns `running` (retry next tick)
  - else returns `failure`

## Leaf Semantics

## Conditions (`cond`)

- callback signature returns `bool`
- `true -> success`
- `false -> failure`

## Actions (`act`)

- callback signature returns BT status
- may return `running` across ticks
- can use `node_memory` to persist progress

## Planner Leaf (`plan-action`)

- reads state from configured blackboard key
- runs planner with bounded budget (`budget_ms`) and work cap (`work_max`)
- writes chosen action to configured blackboard key
- optionally writes compact metadata JSON to `meta_key`
- returns `success` only when planner status is `:ok`
- returns `failure` when planner status is `:timeout`, `:error`, `:noaction`, or when state/config/model is invalid

## VLA Leaves

## `vla-request`

- validates request inputs from configured keys/options
- submits async job through [host](../terminology.md#host) VLA service
- writes `job_id` to blackboard
- returns `running`

## `vla-wait`

- polls `job_id` through [host](../terminology.md#host) VLA service
- while queued/running/streaming: returns `running`
- on valid terminal action: writes action to blackboard and returns `success`
- on timeout/error/cancel/invalid: returns `failure` (and clears `job_id` unless configured otherwise)

## `vla-cancel`

- cancels job id at configured key when present
- clears `job_id` key
- returns `success` for idempotent control flow

## Error And Missing Callback Semantics

If a condition/action callback is missing, runtime:

- emits trace/log error
- returns `failure`

If callback throws, runtime:

- catches exception at BT boundary
- emits trace/log error
- returns `failure`

## Reset Semantics

`bt.reset` clears:

- per-node memory
- blackboard

It does not implicitly clear trace/log buffers.

## Authoring And Persistence Notes

- `(bt ...)` and `(defbt ...)` compile BT language (DSL: a small purpose-built language for behaviour trees) at evaluation time.
- `bt.compile` is equivalent low-level compilation over quoted DSL data.
- `bt.to-dsl` returns canonical DSL data for a compiled definition.
- `bt.save-dsl` / `bt.load-dsl` are the recommended portable persistence path.
- `bt.save` / `bt.load` provide versioned compiled serialisation (`MBT1`).

## Common Gotchas

- `seq` and `sel` are memoryless.
- `mem-seq` and `mem-sel` retain child progress in per-node instance memory.
- `reactive-seq` and `reactive-sel` require haltable running leaves for clean pre-emption.
- `async-seq` yields between steps by design; this is expected.
- `running` is a status, not a thread.
- long-running leaves should avoid blocking the tick thread.

## See Also

- [PlanAction Node Reference](plan-action-node.md)
- [Planner Configuration Reference](planner-configuration.md)
- [VLA BT Nodes](vla-nodes.md)
- [BT.CPP Crosswalk](cpp-crosswalk.md)
