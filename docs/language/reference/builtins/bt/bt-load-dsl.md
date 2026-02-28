# `bt.load-dsl`

**Signature:** `(bt.load-dsl "path") -> bt_def`

## What It Does

Loads BT DSL file and compiles it.

## Arguments And Return

- Arguments: path string
- Return: bt_def

## Errors And Edge Cases

- Path/IO/parse/compile errors.

## Examples

### Minimal

```lisp
(bt.load-dsl "tree.lisp")
```

### Realistic

```lisp
(begin (define d (bt.load-dsl "tree.lisp")) (bt.new-instance d))
```

## Notes

- Companion to `bt.save-dsl`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
