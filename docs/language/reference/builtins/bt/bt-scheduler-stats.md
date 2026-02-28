# `bt.scheduler.stats`

**Signature:** `(bt.scheduler.stats) -> string`

## What It Does

Returns scheduler counters and timings.

## Arguments And Return

- Arguments: none
- Return: string

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(bt.scheduler.stats)
```

### Realistic

```lisp
(begin (define d (bt (act async-sleep-ms 5))) (define i (bt.new-instance d)) (bt.tick i) (bt.scheduler.stats))
```

## Notes

- Useful for async behaviour diagnostics.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
