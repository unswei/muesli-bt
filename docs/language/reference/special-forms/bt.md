# `bt`

**Signature:** `(bt dsl-form) -> bt_def`

## What It Does

Compiles one BT DSL form.

## Arguments And Return

- Arguments: one BT DSL form
- Return: `bt_def`

## Errors And Edge Cases

- BT compile errors are surfaced as runtime errors.

## Examples

### Minimal

```lisp
(bt (succeed))
```

### Realistic

```lisp
(begin (define d (bt (seq (cond always-true) (act always-success)))) d)
```

## Notes

- Authoring-friendly compile form.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
