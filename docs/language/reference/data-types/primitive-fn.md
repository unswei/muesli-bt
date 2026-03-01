# `primitive_fn`

**Signature:** `runtime primitive function`

## What It Does

[Host](../../../terminology.md#host)-implemented callable primitive value.

## Arguments And Return

- Return: callable function value

## Errors And Edge Cases

- Not serializable via `write`/`save`.

## Examples

### Minimal

```lisp
+
```

### Realistic

```lisp
(begin (define add +) (add 1 2))
```

## Notes

- Installed in global environment.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
