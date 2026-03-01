# Blackboard Design And Usage

The blackboard is per-instance shared state for BT leaves.

Scope model:

- per-instance only
- no process-global shared blackboard
- no subtree-local scoped blackboard yet

## What Belongs In The Blackboard

Good candidates:

- task flags (`target-visible`)
- numeric goals and thresholds
- action progress markers
- integration results from [host](../terminology.md#host) actions

Avoid:

- storing unrelated meanings under one key
- large opaque payloads without clear ownership

## Value Types

Supported blackboard value variants:

- `nil`
- `bool`
- `int64`
- `double`
- `string`
- `float64[]`
- `image_handle`
- `blob_handle`

## Metadata Tracked Per Entry

Each key stores:

- current value
- value type
- `last_write_tick`
- `last_write_ts`
- `last_writer_node_id`
- optional `last_writer_name`

`bt.blackboard.dump` prints this metadata in text form.

## Read/Write Flow

- leaves read via `ctx.bb_get(key)`
- leaves write via `ctx.bb_put(key, value, writer-name)`
- optional tick input can seed/update values:

```lisp
(bt.tick inst '((goal-x 10) (goal-y 20)))
```

## Inspectability And Tracing

Inspectable means you can:

- view current key/value state (`bt.blackboard.dump`)
- inspect write history through trace events (`bb_write`)
- identify writer node id/name from metadata and trace

Optional read tracing (`bb_read`) can be enabled with:

```lisp
(bt.set-read-trace-enabled inst #t)
```

## Practical Key Conventions

Recommended style:

- kebab-case keys (`target-visible`, `goal-x`)
- one stable meaning per key
- include subsystem prefix when useful (`nav-goal-x`, `gripper-state`)

## Robotics Example

- `target-visible`: bool from perception
- `approach-retries`: int from action logic
- `grasp-last-error`: string from grasp wrapper
- `ptz-state`: float vector state for planner loops
- `obs-image`: image handle for async VLA requests

## See Also

- [BT Semantics](semantics.md)
- [Tracing](../observability/tracing.md)
- [Roadmap](../limitations-roadmap.md)
