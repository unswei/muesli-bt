# `vla.set-log-enabled`

**Signature:** `(vla.set-log-enabled enabled?) -> nil`

## What It Does

Enables or disables VLA JSON lines file output.

## Arguments And Return

- Arguments: boolean
- Return: `nil`

## Errors And Edge Cases

- non-boolean input raises runtime error

## Examples

### Minimal

```lisp
(vla.set-log-enabled #t)
```

### Realistic

```lisp
(begin
  (vla.set-log-path "logs/vla.jsonl")
  (vla.set-log-enabled #f))
```

## Notes

- In-memory records remain available via `vla.logs.dump`.

## See Also

- [Reference Index](../../index.md)
- [vla.logs.dump](vla-logs-dump.md)
