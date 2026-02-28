# `bt.trace.snapshot`

**Signature:** `(bt.trace.snapshot inst) -> string`

## What It Does

Returns current trace snapshot text.

## Arguments And Return

- Arguments: bt_instance
- Return: string

## Errors And Edge Cases

- Handle/type validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.trace.snapshot i))
```

### Realistic

```lisp
(begin (defbt t (act running-then-success 1)) (define i (bt.new-instance t)) (bt.tick i) (bt.trace.snapshot i))
```

## Notes

- Current implementation matches dump output path.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
