# `env.act`

**Signature:** `(env.act action-map) -> nil`

## What It Does

Submits the action for the next control tick.

## Arguments And Return

- Arguments: action map
  - canonical form uses `action_schema`
  - many backends also require `t_ms`
  - payload shape under `u` is backend/schema specific
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
  (define u (map.make))
  (map.set! action 'action_schema "ros2.action.v1")
  (map.set! action 't_ms 0)
  (map.set! u 'linear_x 0.1)
  (map.set! u 'linear_y 0.0)
  (map.set! u 'angular_z -0.1)
  (map.set! action 'u u)
  (env.act action))
```

## Notes

- Bounds, clamping, rejection, and fallback behaviour are backend/schema dependent.

## See Also

- [Reference Index](../../index.md)
- [env.step](env-step.md)
