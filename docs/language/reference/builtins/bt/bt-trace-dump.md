# `bt.trace.dump`

**Signature:** `(bt.trace.dump inst) -> string`

## What It Does

Returns trace event dump text.

## Arguments And Return

- Arguments: bt_instance
- Return: string

## Errors And Edge Cases

- Handle/type validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.trace.dump i))
```

### Realistic

```lisp
(begin (defbt t (act running-then-success 1)) (define i (bt.new-instance t)) (bt.tick i) (bt.trace.dump i))
```

## Notes

- Useful during debugging.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
