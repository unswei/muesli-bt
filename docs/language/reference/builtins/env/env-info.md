# `env.info`

**Signature:** `(env.info) -> map`

## What It Does

Returns environment capability metadata for the fixed `env.*` interface.

## Arguments And Return

- Arguments: none
- Return: map with fields:
  - `api_version` (string, currently `"env.api.v1"`)
  - `attached` (boolean)
  - `backend` (string or `nil`)
  - `backend_version` (string or `nil`)
  - `supports` (map of booleans like `reset`, `debug_draw`, `headless`, `realtime_pacing`, `deterministic_seed`)
  - optional `notes` (string)

## Errors And Edge Cases

- none (always available even if no backend is attached)

## Examples

### Minimal

```lisp
(env.info)
```

### Realistic

```lisp
(begin
  (env.attach "pybullet")
  (map.get (env.info) 'supports (map.make)))
```

## Notes

- `sim.info` is a temporary alias to `env.info`.

## See Also

- [Reference Index](../../index.md)
- [env.attach](env-attach.md)

