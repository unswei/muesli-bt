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

This split allows one definition to be shared across many instances.

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

Quick table:

| Child status | `invert` result |
| --- | --- |
| `success` | `failure` |
| `failure` | `success` |
| `running` | `running` |

## `repeat n`

Uses per-node counter memory.

- if counter already `>= n`, returns `success`
- tick child otherwise
- child `failure` -> `failure`
- child `running` -> `running`
- child `success` increments counter
  - if counter reaches `n`, returns `success`
  - otherwise returns `running`

Note: `n == 0` returns `success` immediately.

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

## Error And Missing Callback Semantics

If condition/action callback is missing, runtime:

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

## Common Gotchas

- BT forms must be quoted data in v1.
- `seq` and `sel` are memoryless in v1.
- `running` is not a thread; it is a status that indicates unfinished work.
- long-running leaves should avoid blocking the tick thread.
