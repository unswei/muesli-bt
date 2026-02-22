# Logging

## Logging Architecture

The runtime supports in-memory logging through a bounded ring sink.

Key components:

- `log_record`
- `log_sink` interface
- `memory_log_sink` default implementation

Each record includes:

- sequence
- timestamp (`ts_ns` in dumps)
- level (`debug`, `info`, `warn`, `error`)
- tick index
- node id
- category
- message

## Where Logs Go

By default, `runtime_host` keeps logs in memory.

Inspect from Lisp:

```lisp
(bt.logs.dump)
(bt.logs.snapshot)
(bt.log.dump)      ; alias
(bt.log.snapshot)  ; alias
```

## Typical Categories

Observed categories include:

- `bt`
- `scheduler`

## Practical Workflow

1. clear old logs

```lisp
(bt.clear-logs)
```

2. run ticks
3. inspect dump and filter by `category=` or `level=`

## Notes

- logging is optional at runtime level (sink may be null)
- current sink is in-memory only; external adapters are future work
