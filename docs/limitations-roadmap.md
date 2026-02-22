# Limitations, Trade-Offs, And Roadmap

## Current Limitations

- no `set!`, `set-car!`, or `set-cdr!`
- no macros in v1
- memoryless `seq` and `sel` only
- no explicit leaf `halt` contract in v1
- blackboard value types are intentionally small (`nil/bool/int64/double/string`)
- trace/log ring buffers are simple bounded in-memory stores

## Why These Choices Were Made

- keep semantics easy to reason about
- keep host integration boundaries small
- ship usable BT authoring and runtime behaviour early
- prioritise inspectability over framework complexity

## Clear Line: Implemented vs Planned

Implemented now:

- phases 1-6 from `muesli-bt.md`
- typed host services (`clock_interface`, `robot_interface`)
- sample robotics wrappers (`battery-ok`, `target-visible`, `approach-target`, `grasp`, `search-target`)

Planned later (not v1):

- explicit `halt` semantics for long-running leaves
- memoryful composite variants (`mem-seq`, `mem-sel`)
- richer blackboard schemas and host object handles
- macro-based authoring convenience (`(bt ...)`)
- external telemetry exporters and richer profiling pipeline
