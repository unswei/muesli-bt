# tick audit record

## what this is

The tick audit record is the `v0.7.0` measurement payload for proving tick hot-path allocation, heap, garbage collection (GC), node-path, and logging behaviour.

The target canonical event type is `tick_audit`.
The target payload schema is `tick_audit.v1`.

The runtime emits this event from `bt.tick` when tick audit mode is enabled.

## when to use it

Use tick audit records when you need to answer these questions:

- did this tick allocate after warm-up?
- how many bytes were allocated during the measured tick window?
- did GC run during the tick?
- how did heap live bytes change across the tick?
- which node path was active at the end of the tick?
- was canonical logging disabled, buffered, file-backed, listener-backed, or flushed per message?

Do not use tick audit records as hard real-time proof.
They measure a task-level control runtime.
They do not prove servo-loop suitability.

## how it works

The runtime emits one `tick_audit` event per tick when audit mode is enabled.
The event is emitted after `tick_end` for the same tick.

The measured window starts immediately before `tick_begin` is emitted.
The measured window ends immediately after root evaluation and `tick_end` data have been prepared.
Serialising and writing the `tick_audit` event itself is outside the measured tick window.

This boundary keeps the record useful for two modes:

- logging excluded: measures BT execution, callbacks, planner/VLA decision points, and ordinary event creation before final audit serialisation
- logging included: measures normal canonical logging work inside the tick, but still excludes the audit record's own serialisation

The audit record should be emitted only after warm-up when strict zero-allocation claims are being tested.
Warm-up includes parsing, BT compilation, instance creation, symbol interning, node-state allocation, blackboard setup, and expected buffer preallocation.

## api / syntax

Enable runtime emission with:

```lisp
(events.enable-tick-audit #t)
```

Disable it with:

```lisp
(events.enable-tick-audit #f)
```

### event envelope

The canonical envelope uses the normal `mbt.evt.v1` fields:

```json
{
  "schema": "mbt.evt.v1",
  "contract_version": "1.0.0",
  "type": "tick_audit",
  "run_id": "run-1735689600000",
  "unix_ms": 1735689600124,
  "seq": 43,
  "tick": 7,
  "data": {}
}
```

### required payload fields

- `schema_version`: `"tick_audit.v1"`
- `tick_id`: tick index as an integer; must match the envelope `tick`
- `measurement_window`: `"tick_pre_begin_to_tick_end_pre_audit"`
- `allocation_count`: number of heap allocations observed in the measured window
- `allocation_bytes`: total allocated bytes observed in the measured window
- `heap_live_bytes_before`: live heap bytes at measurement start
- `heap_live_bytes_after`: live heap bytes at measurement end
- `heap_live_bytes_delta`: `heap_live_bytes_after - heap_live_bytes_before`
- `gc_collections_delta`: number of GC collections completed during the measured window
- `gc_pause_ns_delta`: total GC pause nanoseconds accumulated during the measured window
- `gc_live_objects_after`: live object count after the last completed collection, or current live object count when exact live counting is available
- `gc_freed_objects_delta`: number of objects freed by collections during the measured window
- `gc_forced`: boolean, true when any collection in the measured window was explicitly forced
- `root_node_id`: root BT node id
- `root_status`: root status at tick end: `"running"`, `"success"`, `"failure"`, or `"invalid"`
- `node_path`: list of node ids from the root to the active or terminal node path used for the tick summary
- `logging_mode`: logging mode summary, described below
- `audit_mode`: audit mode summary, described below

### optional payload fields

- `terminal_node_id`: terminal node id when the tick ended on a clear terminal leaf
- `active_async_jobs`: number of runtime-tracked async jobs after the tick
- `planner_calls`: number of planner calls started during the tick
- `vla_submits`: number of VLA jobs submitted during the tick
- `vla_polls`: number of VLA polls during the tick
- `fallback_used`: boolean, true when the tick used a runtime or host fallback action
- `deadline_missed`: boolean, true when the tick exceeded `tick_budget_ms`
- `tick_budget_ms`: configured tick budget when present
- `tick_elapsed_ns`: measured tick elapsed time in nanoseconds
- `violation`: short string such as `"allocation"`, `"tick_gc"`, `"deadline"`, or `"none"`
- `notes`: short human-readable diagnostic string

`violation` is ordered by contract severity.
Allocation violations take precedence when strict allocation mode is active and warm-up is complete.
Any GC collection completed inside the measured tick window is reported as `"tick_gc"`.
Deadline misses are reported after allocation and GC checks.

### logging mode

`logging_mode` is a map.

Required fields:

- `enabled`: boolean
- `sink`: `"disabled"`, `"ring"`, `"file"`, `"listener"`, `"file+listener"`, or `"custom"`
- `flush_policy`: `"tick_end"`, `"each_message"`, `"manual"`, or `"none"`
- `includes_canonical_events_in_window`: boolean

Optional fields:

- `ring_size`: configured ring capacity when a ring sink is enabled
- `event_log_path`: file path when a file sink is enabled
- `dropped_events_delta`: events dropped during the measured window when a bounded sink can drop

### audit mode

`audit_mode` is a map.

Required fields:

- `enabled`: boolean
- `strict_allocations`: boolean
- `strict_gc`: boolean
- `warmup_complete`: boolean

Optional fields:

- `allowed_allocation_count`: integer threshold for non-strict or transition runs
- `allowed_allocation_bytes`: integer threshold for non-strict or transition runs
- `gc_policy`: `"default"`, `"between-ticks"`, `"manual"`, or `"fail-on-tick-gc"`

## example

```json
{
  "schema_version": "tick_audit.v1",
  "tick_id": 7,
  "measurement_window": "tick_pre_begin_to_tick_end_pre_audit",
  "allocation_count": 0,
  "allocation_bytes": 0,
  "heap_live_bytes_before": 245760,
  "heap_live_bytes_after": 245760,
  "heap_live_bytes_delta": 0,
  "gc_collections_delta": 0,
  "gc_pause_ns_delta": 0,
  "gc_live_objects_after": 1536,
  "gc_freed_objects_delta": 0,
  "gc_forced": false,
  "root_node_id": 1,
  "root_status": "running",
  "node_path": [1, 4, 9],
  "terminal_node_id": 9,
  "active_async_jobs": 1,
  "planner_calls": 0,
  "vla_submits": 0,
  "vla_polls": 1,
  "fallback_used": false,
  "deadline_missed": false,
  "tick_budget_ms": 20,
  "tick_elapsed_ns": 420000,
  "violation": "none",
  "logging_mode": {
    "enabled": true,
    "sink": "file",
    "flush_policy": "tick_end",
    "includes_canonical_events_in_window": true,
    "event_log_path": "runs/example/events.jsonl"
  },
  "audit_mode": {
    "enabled": true,
    "strict_allocations": true,
    "strict_gc": true,
    "warmup_complete": true,
    "allowed_allocation_count": 0,
    "allowed_allocation_bytes": 0,
    "gc_policy": "fail-on-tick-gc"
  }
}
```

## gotchas

- Current `allocation_count` and `allocation_bytes` values come from the Lisp GC heap counters. They are useful runtime evidence, but full C++ allocation enforcement still needs the benchmark lane's allocation tracker.
- Current `heap_live_bytes_after` follows the Lisp GC heap counter exposed as `bytes_allocated`.
- `gc_pause_ns_delta` is cumulative for collections completed during the tick. A later `gc_begin` / `gc_end` event pair can provide per-collection detail.
- `gc_begin` and `gc_end` are implemented before `tick_audit`; use them first to verify GC policy and pause measurement.
- Under `fail-on-tick-gc`, forced or requested collection inside a tick raises before `gc_begin`.
  The strict-mode proof is therefore the absence of any `gc_begin` or `gc_end` event with `"in_tick":true`, plus `tick_audit` rows with `"strict_gc":true` and `"gc_collections_delta":0`.
- `node_path` is a compact tick summary. It does not replace `node_enter` and `node_exit` events.
- Logging mode must be recorded because canonical logging can be the dominant source of allocation if buffers are not preallocated.
- Strict zero-allocation claims only apply after warm-up. The audit record must say whether warm-up is complete.

## see also

- [canonical event log](event-log.md)
- [profiling and performance](profiling.md)
- [runtime contract v1](../contracts/runtime-contract-v1.md)
- [runtime performance](../internals/runtime-performance.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
