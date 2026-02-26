# Limitations, Trade-Offs, And Roadmap

## Current Limitations

- no `set!`, `set-car!`, or `set-cdr!`
- no general macro system (`defmacro` is not implemented)
- memoryless `seq` and `sel` only
- no explicit leaf `halt` contract
- blackboard value types are intentionally small (`nil/bool/int64/double/string`)
- trace/log ring buffers are bounded in-memory stores

## Completed Recently

- BT authoring sugar: `(bt ...)`, `(defbt ...)`
- quasiquote reader/evaluator support: backquote, unquote, unquote-splicing
- Lisp convenience forms: `let`, `cond`
- source loading: `(load "file.lisp")`
- readable serialisation: `write`, `write-to-string`, `save`
- BT DSL portability APIs: `bt.to-dsl`, `bt.save-dsl`, `bt.load-dsl`
- compiled BT serialisation with versioned `MBT1` format: `bt.save`, `bt.load`

## Why These Choices Were Made

- keep semantics easy to reason about
- keep host integration boundaries small
- ship usable BT authoring/runtime behaviour early
- prioritise inspectability over framework complexity

## Planned Later

- explicit `halt` semantics for long-running leaves
- memoryful composite variants (`mem-seq`, `mem-sel`)
- richer blackboard schemas and host object handles
- richer macro facilities beyond quasiquote templates
- external telemetry exporters and deeper profiling pipeline
