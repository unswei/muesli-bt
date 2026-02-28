# `planner.set-log-enabled`

**Signature:** `(planner.set-log-enabled enabled-bool) -> nil`

## What It Does

Enables or disables planner JSON lines file writing.

## Arguments And Return

- Arguments: boolean
- Return: `nil`

## Errors And Edge Cases

- type/arity validation errors

## Examples

### Minimal

```lisp
(planner.set-log-enabled #t)
```

### Realistic

```lisp
(begin
  (planner.set-log-enabled #f)
  (planner.set-log-enabled #t))
```

## Notes

- In-memory planner records remain available through `planner.logs.dump`.

## See Also

- [Reference Index](../../index.md)
- [Planner Logging Schema](../../../../observability/planner-logging.md)
