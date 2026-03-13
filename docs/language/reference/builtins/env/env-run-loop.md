# `env.run-loop`

**Signature:** `(env.run-loop config-map on-tick-fn) -> result-map`

## What It Does

Runs a [host](../../../../terminology.md#host)-managed control loop around `observe -> on_tick -> act -> step` with fallback/error handling.

Supports single-episode and multi-episode execution.

## Arguments And Return

- Arguments:
  - `config-map` with required keys `tick_hz`, `max_ticks`
  - `on-tick-fn` callable receiving one argument: observation map
- Optional config keys: `episode_max`, `step_max`, `steps_per_tick`, `seed`, `realtime`, `safe_action`, `stop_on_success`, `success_predicate`, `log_path`, `observer`
- Return map includes:
  - `status` in `:ok | :stopped | :error | :unsupported`
  - `last_status`
  - `ok`
  - `error`
  - `episodes_completed`
  - `steps_total`
  - `last_episode_steps`
  - compatibility keys: `episodes`, `ticks`
  - `message`, `reason`
  - `final_obs`
  - `fallback_count`
  - `overrun_count`

## Episode Semantics

- `episode_max` defaults to `1`.
- `step_max` is the per-episode step cap.
- If `step_max` is not provided, `max_ticks` is used as the per-episode cap.
- For reset-capable backends, each episode starts with `env.reset` and its own step counter.
- If `episode_max > 1` and backend reset is unsupported, `env.run-loop` returns `status :unsupported` with message `episode_max>1 requires env.reset capability`.

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
  (map.set! cfg 'step_max 100)
  (map.set! cfg 'safe_action safe)
  (env.run-loop cfg (lambda (obs) safe)))
```

### Multi-Episode

```lisp
(begin
  (define cfg (map.make))
  (map.set! cfg 'tick_hz 20)
  (map.set! cfg 'max_ticks 200)
  (map.set! cfg 'step_max 200)
  (map.set! cfg 'episode_max 3)
  (map.set! cfg 'realtime #t)
  (env.run-loop
    cfg
    (lambda (obs)
      (let ((a (map.make)))
        (map.set! a 'action_schema "demo.action.v1")
        (map.set! a 'u (list 0.1 0.0))
        a))))
```

### Observer Callback Pattern

Use `observer` when you want per-tick analytics without changing `on_tick` return values.

```lisp
(begin
  (define stats (map.make))
  (map.set! stats 'ticks 0)
  (map.set! stats 'fallback_ticks 0)
  (map.set! stats 'overrun_ticks 0)

  (define observer
    (lambda (record)
      (begin
        (map.set! stats 'ticks (+ (map.get stats 'ticks 0) 1))
        (if (map.get record 'used_fallback #f)
            (map.set! stats 'fallback_ticks (+ (map.get stats 'fallback_ticks 0) 1))
            nil)
        (if (map.get record 'overrun #f)
            (map.set! stats 'overrun_ticks (+ (map.get stats 'overrun_ticks 0) 1))
            nil))))

  (define cfg (map.make))
  (define safe (map.make))
  (map.set! safe 'action_schema "demo.action.v1")
  (map.set! safe 'u (list 0.0 0.0))
  (map.set! cfg 'tick_hz 20)
  (map.set! cfg 'max_ticks 40)
  (map.set! cfg 'safe_action safe)
  (map.set! cfg 'observer observer)
  (map.set! cfg 'log_path "logs/run-loop-observer.jsonl")

  (define result
    (env.run-loop
      cfg
      (lambda (obs)
        (let ((a (map.make)))
          (map.set! a 'action_schema "demo.action.v1")
          (map.set! a 'u (list 0.1 0.0))
          a))))

  (list result stats))
```

### Multi-Episode Analytics

`env.run-loop` returns summary counters that are stable across backends:

```lisp
(begin
  (define cfg (map.make))
  (define safe (map.make))
  (map.set! safe 'action_schema "demo.action.v1")
  (map.set! safe 'u (list 0.0 0.0))
  (map.set! cfg 'tick_hz 20)
  (map.set! cfg 'max_ticks 300)
  (map.set! cfg 'step_max 100)
  (map.set! cfg 'episode_max 3)
  (map.set! cfg 'safe_action safe)

  (define result
    (env.run-loop
      cfg
      (lambda (obs)
        (let ((a (map.make)))
          (map.set! a 'action_schema "demo.action.v1")
          (map.set! a 'u (list 0.1 0.0))
          a))))

  (define episodes (map.get result 'episodes_completed 0))
  (define steps (map.get result 'steps_total 0))
  (define analytics (map.make))
  (map.set! analytics 'episodes_completed episodes)
  (map.set! analytics 'steps_total steps)
  (map.set! analytics 'last_episode_steps (map.get result 'last_episode_steps 0))
  (map.set! analytics 'fallback_count (map.get result 'fallback_count 0))
  (map.set! analytics 'overrun_count (map.get result 'overrun_count 0))
  (if (> episodes 0)
      (map.set! analytics 'avg_steps_per_episode (/ steps episodes))
      (map.set! analytics 'avg_steps_per_episode 0.0))
  analytics)
```

## Notes

- If `on_tick` overruns, runtime uses last-good or safe action and continues.
- On runtime error, one safety action + one step is attempted before returning `:error`.
- `fallback_count` tracks ticks where a fallback or safety action was actually used.
- `stop_on_success` controls whether success ends the run early or continues until `episode_max`/`step_max`.

## See Also

- [Reference Index](../../index.md)
- [env.observe](env-observe.md)
- [env.act](env-act.md)
