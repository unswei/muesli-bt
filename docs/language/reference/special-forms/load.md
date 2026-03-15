# `load`

**Signature:** `(load "path") -> any`

## What It Does

Loads and evaluates all forms from a file in current environment.

## Arguments And Return

- Arguments: path string
- Return: value of final form or `nil`

## Errors And Edge Cases

- Path must be a string; IO/parse/eval failures raise errors.

## Examples

### Minimal

```lisp
(load "examples/repl_scripts/lisp-basics.lisp")
```

### Realistic

```lisp
(begin (load "examples/bt/hello_bt.lisp") nil)
```

## Notes

- Errors include file path context.
- If a loaded file calls `load` again with a relative path, that nested path resolves relative to the directory of the file that issued the nested `load`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
