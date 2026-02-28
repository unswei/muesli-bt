# `env.run-loop`

**Signature:** `(env.run-loop config-map on-tick-fn) -> result-map`

## What It Does

Runs a host-managed control loop around `observe -> on_tick -> act -> step` with fallback/error handling.

## Arguments And Return

- Arguments:
  - `config-map` with required keys `tick_hz`, `max_ticks`
  - `on-tick-fn` callable receiving one argument: observation map
- Optional config keys: `episode_max`, `steps_per_tick`, `seed`, `realtime`, `safe_action`, `stop_on_success`, `success_predicate`, `log_path`, `observer`
- Return map includes:
  - `status` in `:ok | :stopped | :error`
  - `episodes`
  - `ticks`
  - `reason`
  - `final_obs`
  - `fallback_count`
  - `overrun_count`

## Errors And Edge Cases

- backend not attached
- missing required config keys
- invalid config types
- `on_tick` returns no usable action and no `safe_action` is configured

## Examples

### Minimal

```lisp
(begin
  (define cfg (map.make))
  (define safe (map.make))
  (map.set! safe 'action_schema "racecar.action.v1")
  (map.set! safe 'u (list 0.0 0.0))
  (map.set! cfg 'tick_hz 20)
  (map.set! cfg 'max_ticks 100)
  (map.set! cfg 'safe_action safe)
  (env.run-loop cfg (lambda (obs) safe)))
```

### Realistic

```lisp
(begin
  (define cfg (map.make))
  (map.set! cfg 'tick_hz 50)
  (map.set! cfg 'max_ticks 500)
  (map.set! cfg 'realtime #t)
  (env.run-loop
    cfg
    (lambda (obs)
      (let ((a (map.make)))
        (map.set! a 'action_schema "demo.action.v1")
        (map.set! a 'u (list 0.1 0.0))
        a))))
```

## Notes

- If `on_tick` overruns, runtime uses last-good or safe action and continues.
- On runtime error, one safety action + one step is attempted before returning `:error`.
- `sim.run-loop` is a temporary alias.

## See Also

- [Reference Index](../../index.md)
- [env.observe](env-observe.md)
- [env.act](env-act.md)

