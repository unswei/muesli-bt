# `env.configure`

**Signature:** `(env.configure opts-map) -> nil`

## What It Does

Applies backend/runtime options before stepping or loop execution.

## Arguments And Return

- Arguments: map of options

    - common runtime keys include `tick_hz`, `steps_per_tick`, `seed`, `headless`, `realtime`, `log_path`
    - backend-specific keys are documented per backend

- Return: `nil`

## Errors And Edge Cases

- backend not attached
- argument is not a map
- invalid option types (for example non-integer `tick_hz`)
- some backends reject unknown backend-specific keys rather than ignoring them

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
  (map.set! cfg 'control_hz 50)
  (map.set! cfg 'obs_source "odom")
  (map.set! cfg 'action_sink "cmd_vel")
  (map.set! cfg 'reset_mode "stub")
  (env.configure cfg))
```

## Notes

- Keep runtime-generic keys and backend-specific keys explicit in docs.
- ROS2 bring-up config is documented in the integration docs and rejects malformed or unknown backend-specific keys.

## See Also

- [Reference Index](../../index.md)
- [env.run-loop](env-run-loop.md)
