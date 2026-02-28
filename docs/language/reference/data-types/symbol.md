# `symbol`

**Signature:** `identifier token`

## What It Does

Interned symbolic name for bindings or symbolic data.

## Arguments And Return

- Return: symbol or bound value depending on evaluation position

## Errors And Edge Cases

- Unbound symbol lookup raises `name_error`.

## Examples

### Minimal

```lisp
foo
```

### Realistic

```lisp
'foo
```

## Notes

- Symbols are interned.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
