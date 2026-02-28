# `planner.set-base-seed`

**Signature:** `(planner.set-base-seed seed-int) -> nil`

## What It Does

Sets planner base seed used for derived per-tick seeds.

## Arguments And Return

- Arguments: non-negative integer seed
- Return: `nil`

## Errors And Edge Cases

- seed must be non-negative integer

## Examples

### Minimal

```lisp
(planner.set-base-seed 4242)
```

### Realistic

```lisp
(begin
  (planner.set-base-seed 4242)
  (planner.get-base-seed))
```

## Notes

- `plan-action` derives seed from base seed + node name + tick index when `seed_key` is not set.

## See Also

- [Reference Index](../../index.md)
- [Planner Configuration](../../../../bt/planner-configuration.md)
