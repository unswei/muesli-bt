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
- `running` is a status, not a thread.
- long-running leaves should avoid blocking the tick thread.

## See Also

- [PlanAction Node Reference](plan-action-node.md)
- [Planner Configuration Reference](planner-configuration.md)
- [VLA BT Nodes](vla-nodes.md)
