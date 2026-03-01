# `env.act`

**Signature:** `(env.act action-map) -> nil`

## What It Does

Submits the action for the next control tick.

## Arguments And Return

- Arguments: action map (canonical form uses `action_schema` and `u`)
- Return: `nil`

## Errors And Edge Cases

- backend not attached
- action map fails backend validation (missing keys, wrong dimensions/types)

## Examples

### Minimal

```lisp
(begin
  (define a (map.make))
  (map.set! a 'action_schema "racecar.action.v1")
  (map.set! a 'u (list 0.0 0.2))
  (env.act a))
```

### Realistic

```lisp
(begin
  (define action (map.make))
  (map.set! action 'action_schema "demo.action.v1")
  (map.set! action 'u (list 0.1 -0.1 0.0))
  (map.set! action 'mode ':velocity)
  (env.act action))
```

## Notes

- Bounds/clamping behaviour is backend/schema dependent.

## See Also

- [Reference Index](../../index.md)
- [env.step](env-step.md)
