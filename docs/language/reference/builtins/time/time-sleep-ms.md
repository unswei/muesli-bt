# `time.sleep-ms`

**Signature:** `(time.sleep-ms milliseconds) -> nil`

## What It Does

Blocks the current Lisp evaluation thread for at least the requested number of milliseconds.

## Arguments And Return

- Arguments: one non-negative integer millisecond duration
- Return: `nil`

## Errors And Edge Cases

- Negative durations raise a runtime error.
- Non-integer durations raise a runtime error.
- `(time.sleep-ms 0)` is a no-op.

## Examples

### Minimal

```lisp
(time.sleep-ms 10)
```

### Realistic

```lisp
(begin
  (define job (vla.submit req))
  (time.sleep-ms 20)
  (vla.poll job))
```

## Notes

- Use this for scripts, smoke tests, and integration polling.
- Do not use it to change behaviour tree tick semantics.

## See Also

- [time.now-ms](time-now-ms.md)
- [Reference Index](../../index.md)
