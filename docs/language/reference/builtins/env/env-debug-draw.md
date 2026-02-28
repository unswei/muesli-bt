# `env.debug-draw`

**Signature:** `(env.debug-draw payload) -> nil`

## What It Does

Requests optional backend debug visualisation.

## Arguments And Return

- Arguments: backend-defined payload (often a map)
- Return: `nil`

## Errors And Edge Cases

- backend not attached
- backend-specific payload validation errors
- if debug draw is unsupported, call is a no-op

## Examples

### Minimal

```lisp
(env.debug-draw nil)
```

### Realistic

```lisp
(begin
  (define p (map.make))
  (map.set! p 'label "goal")
  (map.set! p 'x 4.0)
  (map.set! p 'y 2.0)
  (env.debug-draw p))
```

## Notes

- Intended for overlays and debug helpers, not core control semantics.
- `sim.debug-draw` is a temporary alias.

## See Also

- [Reference Index](../../index.md)
- [env.info](env-info.md)

