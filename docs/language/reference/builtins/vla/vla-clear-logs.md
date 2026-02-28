# `vla.clear-logs`

**Signature:** `(vla.clear-logs) -> nil`

## What It Does

Clears in-memory VLA record buffer.

## Arguments And Return

- Arguments: none
- Return: `nil`

## Errors And Edge Cases

- none

## Examples

### Minimal

```lisp
(vla.clear-logs)
```

### Realistic

```lisp
(begin
  (vla.clear-logs)
  (vla.logs.dump 10))
```

## Notes

- Does not delete already-written JSON lines file content.

## See Also

- [Reference Index](../../index.md)
- [vla.logs.dump](vla-logs-dump.md)
