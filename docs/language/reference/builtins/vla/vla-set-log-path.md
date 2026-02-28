# `vla.set-log-path`

**Signature:** `(vla.set-log-path path) -> nil`

## What It Does

Sets JSON lines output file path for VLA records.

## Arguments And Return

- Arguments: path string
- Return: `nil`

## Errors And Edge Cases

- empty/non-string path raises runtime error

## Examples

### Minimal

```lisp
(vla.set-log-path "logs/vla-records.jsonl")
```

### Realistic

```lisp
(begin
  (vla.set-log-enabled #t)
  (vla.set-log-path "logs/vla.jsonl"))
```

## Notes

- File appends while logging is enabled.

## See Also

- [Reference Index](../../index.md)
- [vla.set-log-enabled](vla-set-log-enabled.md)
