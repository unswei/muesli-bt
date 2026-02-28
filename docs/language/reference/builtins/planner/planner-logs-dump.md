# `planner.logs.dump`

**Signature:** `(planner.logs.dump [max-count]) -> string`

## What It Does

Returns recent planner records as JSON lines text.

## Arguments And Return

- Arguments: optional non-negative integer limit
- Return: string (newline-delimited JSON records)

## Errors And Edge Cases

- invalid arity or negative count raises runtime errors

## Examples

### Minimal

```lisp
(planner.logs.dump)
```

### Realistic

```lisp
(begin
  (planner.logs.dump 20))
```

## Notes

- Records include timing, iteration counts, confidence, and chosen action.

## See Also

- [Reference Index](../../index.md)
- [Planner Logging Schema](../../../../observability/planner-logging.md)
