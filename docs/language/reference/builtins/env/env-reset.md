# `env.reset`

**Signature:** `(env.reset seed-or-nil) -> obs-map`

## What It Does

Resets the attached backend and returns the first observation map.

## Arguments And Return

- Arguments: integer seed or `nil`
- Return: observation map containing at least `obs_schema`, `t_ms`, `episode`, `step`

## Errors And Edge Cases

- backend not attached
- backend reports reset unsupported
- invalid seed type

## Examples

### Minimal

```lisp
(env.reset nil)
```

### Realistic

```lisp
(begin
  (env.attach "pybullet")
  (define obs (env.reset 7))
  (map.get obs 'obs_schema "none"))
```

## Notes

- `episode` increments on reset in the current runtime.

## See Also

- [Reference Index](../../index.md)
- [env.observe](env-observe.md)
