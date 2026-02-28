# `planner.set-log-path`

**Signature:** `(planner.set-log-path path-string) -> nil`

## What It Does

Sets the JSON lines output file path for planner records.

## Arguments And Return

- Arguments: output path string
- Return: `nil`

## Errors And Edge Cases

- path must be a non-empty string

## Examples

### Minimal

```lisp
(planner.set-log-path "logs/planner-records.jsonl")
```

### Realistic

```lisp
(begin
  (planner.set-log-path "logs/planner-run.jsonl")
  (planner.set-log-enabled #t))
```

## Notes

- This config affects subsequent planner records.

## See Also

- [Reference Index](../../index.md)
- [Planner Logging Schema](../../../../observability/planner-logging.md)
