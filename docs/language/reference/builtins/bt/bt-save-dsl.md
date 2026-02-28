# `bt.save-dsl`

**Signature:** `(bt.save-dsl bt-def "path") -> #t`

## What It Does

Saves canonical DSL to text file.

## Arguments And Return

- Arguments: bt_def and path string
- Return: `#t`

## Errors And Edge Cases

- Handle/path/IO errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (bt.save-dsl d "tree.lisp"))
```

### Realistic

```lisp
(begin (define d (bt (act always-success))) (bt.save-dsl d "patrol.lisp"))
```

## Notes

- Recommended portable persistence path.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
