# `bt.load`

**Signature:** `(bt.load "path.mbt") -> bt_def`

## What It Does

Loads compiled MBT binary definition.

## Arguments And Return

- Arguments: path string
- Return: bt_def

## Errors And Edge Cases

- Path/IO/format/version validation errors.

## Examples

### Minimal

```lisp
(bt.load "tree.mbt")
```

### Realistic

```lisp
(begin (define d (bt.load "tree.mbt")) (bt.new-instance d))
```

## Notes

- Fast startup path for precompiled trees.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
