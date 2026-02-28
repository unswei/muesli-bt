# `bt.set-read-trace-enabled`

**Signature:** `(bt.set-read-trace-enabled inst bool) -> nil`

## What It Does

Enables/disables `bb_read` trace events.

## Arguments And Return

- Arguments: bt_instance, boolean
- Return: nil

## Errors And Edge Cases

- Type/handle validation errors.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.set-read-trace-enabled i #t))
```

### Realistic

```lisp
(begin (define d (bt (succeed))) (define i (bt.new-instance d)) (bt.set-read-trace-enabled i #f))
```

## Notes

- Use when debugging blackboard-read behavior.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
