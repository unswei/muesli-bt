# `bt.export-dot`

**Signature:** `(bt.export-dot bt-def "path/to/tree.dot") -> #t`

## What It Does

Exports a compiled behaviour tree definition to a Graphviz DOT file.

## Arguments And Return

- Arguments: bt_def and output path string
- Return: `#t`

## Errors And Edge Cases

- Handle/path/type validation errors.
- Fails if output file cannot be opened or written.

## Examples

### Minimal

```lisp
(begin
  (define d (bt (seq (cond always-true) (act always-success))))
  (bt.export-dot d "tree.dot"))
```

### Render with Graphviz

```bash
dot -Tsvg tree.dot -o tree.svg
```

## Notes

- DOT labels include node id, node name (if any), and node type.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
