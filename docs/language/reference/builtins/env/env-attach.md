# `env.attach`

**Signature:** `(env.attach backend-name) -> nil`

## What It Does

Attaches a registered backend to the fixed `env.*` interface.

## Arguments And Return

- Arguments: backend name as string or symbol (for example `"pybullet"`)
- Return: `nil`

## Errors And Edge Cases

- if backend name is unknown
- if backend is already attached
- if argument is not string/symbol

## Examples

### Minimal

```lisp
(env.attach "pybullet")
```

### Realistic

```lisp
(begin
  (env.attach "pybullet")
  (map.get (env.info) 'backend nil))
```

## Notes

- Only one backend can be attached at a time.
- `sim.attach` is a temporary alias.

## See Also

- [Reference Index](../../index.md)
- [env.info](env-info.md)

