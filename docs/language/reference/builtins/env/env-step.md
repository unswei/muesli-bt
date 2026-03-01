# `env.step`

**Signature:** `(env.step) -> bool`

## What It Does

Advances the backend by one control tick.

## Arguments And Return

- Arguments: none
- Return: boolean (`#t` continue, `#f` backend requested stop/termination)

## Errors And Edge Cases

- backend not attached
- backend throws during stepping

## Examples

### Minimal

```lisp
(env.step)
```

### Realistic

```lisp
(begin
  (if (env.step)
      'running
      'stopped))
```

## Notes

- `env.step` is the canonical “advance to next control tick” call.

## See Also

- [Reference Index](../../index.md)
- [env.run-loop](env-run-loop.md)
