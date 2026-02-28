# Example: Bounded-Time Planning (PTZ Tracking)

This script runs a PTZ-style tracking loop:

- `plan-action` calls the `ptz-track` planner model within a per-tick budget
- `apply-planned-ptz` applies the selected `(dpan, dtilt)` action
- the tree keeps ticking until the target is near image center

Script path:

- `examples/repl_scripts/planner-bt-ptz.lisp`

Run:

```bash
./build/dev/muslisp examples/repl_scripts/planner-bt-ptz.lisp
```

Outputs:

- blackboard snapshot with `ptz-state`, `ptz-action`, `ptz-meta`
- planner JSON records via `planner.logs.dump`
