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
(begin
  (define d
    (bt
      (sel
        (seq (vla-wait :name \"policy\" :job_key policy-job :action_key policy-action) (succeed))
        (seq (vla-request :name \"policy\" :job_key policy-job :instruction \"move\" :state_key state) (running)))))
  d)
```

## Notes

- Authoring-friendly compile form.
- BT DSL includes composite/decorator leaves plus planner and VLA node forms.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
- [BT Syntax](../../../bt/syntax.md)
