# `bt_instance`

**Signature:** `BT runtime instance handle`

## What It Does

Mutable per-instance runtime state handle.

## Arguments And Return

- Return: `bt_instance`

## Errors And Edge Cases

- Tick/reset/stats APIs require this type.

## Examples

### Minimal

```lisp
(begin (define d (bt (succeed))) (bt.new-instance d))
```

### Realistic

```lisp
(begin (defbt t (act running-then-success 1)) (define i (bt.new-instance t)) (bt.tick i))
```

## Notes

- Holds blackboard, trace, and node memory.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
