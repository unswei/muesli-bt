# `env.configure`

**Signature:** `(env.configure opts-map) -> nil`

## What It Does

Applies backend/runtime options before stepping or loop execution.

## Arguments And Return

- Arguments: map of options (common keys include `tick_hz`, `steps_per_tick`, `seed`, `headless`, `realtime`, `log_path`)
- Return: `nil`

## Errors And Edge Cases

- backend not attached
- argument is not a map
- invalid option types (for example non-integer `tick_hz`)

## Examples

### Minimal

```lisp
(begin
  (define cfg (map.make))
  (map.set! cfg 'tick_hz 20)
  (env.configure cfg))
```

### Realistic

```lisp
(begin
  (define cfg (map.make))
  (map.set! cfg 'tick_hz 100)
  (map.set! cfg 'steps_per_tick 4)
  (map.set! cfg 'realtime #f)
  (map.set! cfg 'log_path "logs/run.jsonl")
  (env.configure cfg))
```

## Notes

- Unsupported keys may be ignored by a backend.

## See Also

- [Reference Index](../../index.md)
- [env.run-loop](env-run-loop.md)
