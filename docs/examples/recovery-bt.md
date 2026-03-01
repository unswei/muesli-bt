# Example: Fallback/Recovery BT

Path: `examples/bt/recovery-bt.lisp`

```lisp
--8<-- "examples/bt/recovery-bt.lisp"
```

Interpretation:

- `sel` uses left branch as primary path
- if target is not visible, primary branch fails
- fallback `search-target` runs and updates blackboard
- later ticks can re-enter primary branch and complete
