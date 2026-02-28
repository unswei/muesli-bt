# `bt.blackboard.dump`

**Signature:** `(bt.blackboard.dump inst) -> string`

## What It Does

Returns blackboard data and metadata dump text.

## Arguments And Return

- Arguments: bt_instance
- Return: string

## Errors And Edge Cases

- Handle/type validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (cond bb-has foo))) (define i (bt.new-instance d)) (bt.blackboard.dump i))
```

### Realistic

```lisp
(begin (define d (bt (act bb-put-int foo 42))) (define i (bt.new-instance d)) (bt.tick i) (bt.blackboard.dump i))
```

## Notes

- Includes key type and writer metadata.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
