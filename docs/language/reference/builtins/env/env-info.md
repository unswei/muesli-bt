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
    - optional backend-specific metadata such as schema ids, reset policy, capability tags, or config

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
  (env.attach "ros2")
  (list (map.get (env.info) 'backend "")
        (map.get (env.info) 'obs_schema "")
        (map.get (env.info) 'reset_supported #f)))
```

## Notes

- `env.info` is always available, even before backend attachment.
- Backends may add extra metadata fields, but should not replace the stable core fields above.

## See Also

- [Reference Index](../../index.md)
- [env.attach](env-attach.md)
