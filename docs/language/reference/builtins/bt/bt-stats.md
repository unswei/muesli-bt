# `bt.stats`

**Signature:** `(bt.stats inst) -> string`

## What It Does

Returns per-instance profiling summary text.

## Arguments And Return

- Arguments: bt_instance
- Return: string

## Errors And Edge Cases

- Handle/type validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.stats i))
```

### Realistic

```lisp
(begin (defbt t (act running-then-success 1)) (define i (bt.new-instance t)) (bt.tick i) (bt.stats i))
```

## Notes

- Contains tick and node counters/timings.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
