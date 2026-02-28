# `bt.compile`

**Signature:** `(bt.compile dsl-data) -> bt_def`

## What It Does

Compiles quoted BT DSL data.

## Arguments And Return

- Arguments: one DSL data expression
- Return: bt_def

## Errors And Edge Cases

- Compile/type/arity validation errors.

## Examples

### Minimal

```lisp
(bt.compile '(succeed))
```

### Realistic

```lisp
(bt.compile '(seq (cond battery-ok) (act approach-target)))
```

## Notes

- Low-level compile API.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
