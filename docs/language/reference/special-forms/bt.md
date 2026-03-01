# `bt`

**Signature:** `(bt dsl-form) -> bt_def`

## What It Does

Compiles one BT language form (DSL: a small purpose-built language for behaviour trees).

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
- BT DSL includes:
  - memoryless composites: `seq`, `sel`
  - memoryful composites: `mem-seq`, `mem-sel`
  - yielding/reactive composites: `async-seq`, `reactive-seq`, `reactive-sel`
  - decorators: `invert`, `repeat`, `retry`
  - leaves: `cond`, `act`, `plan-action`, `vla-request`, `vla-wait`, `vla-cancel`
  - utility nodes: `succeed`, `fail`, `running`

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
- [BT Syntax](../../../bt/syntax.md)
