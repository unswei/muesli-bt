# Example: Bounded-Time Planning (1D)

This script runs a BT loop where `plan-action` chooses a bounded-time action and `apply-planned-1d` advances state.

Script path:

- `examples/repl_scripts/planner-bt-1d.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/planner-bt-1d.lisp
```

What to look for:

- `state` moves toward the goal
- `action` is updated each tick
- `plan-meta` contains compact planner diagnostics
- `planner.logs.dump` returns JSON lines records

See also:

- [Example: Async VLA Stub BT](vla-stub-bt.md)
- [Example: Hybrid VLA + Planner](hybrid-vla-mcts.md)
