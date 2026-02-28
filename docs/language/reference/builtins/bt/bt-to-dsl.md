# `bt.to-dsl`

**Signature:** `(bt.to-dsl bt-def) -> list`

## What It Does

Converts compiled definition back to canonical DSL data.

## Arguments And Return

- Arguments: bt_def
- Return: DSL list

## Errors And Edge Cases

- Handle/type validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (bt.to-dsl d))
```

### Realistic

```lisp
(begin (define d (bt (seq (cond always-true) (act always-success)))) (write-to-string (bt.to-dsl d)))
```

## Notes

- Useful with DSL save/load.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
