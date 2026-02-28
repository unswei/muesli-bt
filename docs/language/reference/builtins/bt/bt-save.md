# `bt.save`

**Signature:** `(bt.save bt-def "path.mbt") -> #t`

## What It Does

Saves compiled BT definition in MBT binary format.

## Arguments And Return

- Arguments: bt_def and path string
- Return: `#t`

## Errors And Edge Cases

- Handle/path/IO/format errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (bt.save d "tree.mbt"))
```

### Realistic

```lisp
(begin (define d (bt (act always-success))) (bt.save d "fastload.mbt"))
```

## Notes

- Versioned binary (`MBT1`).

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
