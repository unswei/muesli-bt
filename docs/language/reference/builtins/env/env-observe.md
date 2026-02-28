# `env.observe`

**Signature:** `(env.observe) -> obs-map`

## What It Does

Returns the current observation from the attached backend.

## Arguments And Return

- Arguments: none
- Return: observation map, with required runtime-level fields:
  - `obs_schema` (string)
  - `t_ms` (integer)
  - `episode` (integer)
  - `step` (integer)

## Errors And Edge Cases

- backend not attached
- backend returns invalid observation shape/type

## Examples

### Minimal

```lisp
(env.observe)
```

### Realistic

```lisp
(begin
  (define obs (env.observe))
  (list (map.get obs 'obs_schema "none")
        (map.get obs 't_ms -1)
        (map.get obs 'step -1)))
```

## Notes

- Additional fields (`state_vec`, `done`, `reward`, `info`) are backend/demo-defined.
- `sim.observe` is a temporary alias.

## See Also

- [Reference Index](../../index.md)
- [env.act](env-act.md)

