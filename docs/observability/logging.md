# Logging

This page describes the logging surfaces available in muesli-bt.

## Available Features

- bounded in-memory log sink (`memory_log_sink`)
- thread-safe writes via internal mutex
- structured log records (sequence, time, level, tick, node, category, message)
- Lisp dump/clear commands

## Record Structure

Each log record includes:

- `sequence`
- `ts_ns` (steady clock nanoseconds)
- `level` (`debug`, `info`, `warn`, `error`)
- `tick` index
- `node` id
- `category`
- `msg`

## Commands Available Now

| Command | Purpose | Return |
| --- | --- | --- |
| `(bt.logs.dump)` | Dump current logs | string |
| `(bt.logs.snapshot)` | Alias of dump path | string |
| `(bt.log.dump)` | Alias | string |
| `(bt.log.snapshot)` | Alias | string |
| `(bt.clear-logs)` | Clear in-memory sink | `nil` |

## Where Logs Are Emitted Today

The runtime emits logs for:

- BT runtime errors (for example missing callbacks or thrown leaf exceptions)
- tick budget overruns (`warn`, category `bt`)
- scheduler lifecycle diagnostics (`debug`, category `scheduler`)
- scheduler warnings/errors when relevant

## Practical Workflow

1. clear old logs before a focused test run

```lisp
(bt.clear-logs)
```

2. execute a small number of representative ticks

3. inspect logs

```lisp
(bt.logs.dump)
```

4. correlate with trace where needed (`bt.trace.snapshot`) for event ordering

## Example Output Shape

```text
17 level=debug ts_ns=... tick=3 node=5 category=scheduler msg=scheduler started task async-sleep-ms
18 level=warn ts_ns=... tick=9 node=1 category=bt msg=tick budget overrun
19 level=error ts_ns=... tick=10 node=8 category=bt msg=action threw: ...
```

## Notes On Scope

- Log storage is process-local and memory-bounded.
- Oldest records are evicted when sink capacity is reached.
- `runtime_host` owns the default sink; logs are currently not per-instance.
- File sinks and external exporters are roadmap work.

## See Also

- [Tracing](tracing.md)
- [Profiling](profiling.md)
- [Roadmap](../limitations-roadmap.md)
