# `bt.status->symbol`

**Signature:** `(bt.status->symbol sym) -> sym`

## What It Does

Validates status symbol values.

## Arguments And Return

- Arguments: symbol
- Return: same symbol when valid

## Errors And Edge Cases

- Errors on non-symbol or unsupported symbol.

## Examples

### Minimal

```lisp
(bt.status->symbol 'success)
```

### Realistic

```lisp
(begin (define s 'running) (bt.status->symbol s))
```

## Notes

- Accepted: success/failure/running.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
