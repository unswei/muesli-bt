# Example: Hybrid VLA + Planner

This script demonstrates combining VLA proposal logic with host-side planning through `planner.plan`.

Script path:

- `examples/repl_scripts/hybrid-vla-mcts-1d.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/hybrid-vla-mcts-1d.lisp
```

What to look for:

- `vla.submit` + `vla.poll` produce structured proposal metadata
- planner requests use unified `planner.request.v1`
- backend selection is explicit via `planner` (`mcts`, `mppi`, `ilqr`)
