# PyBullet Racecar: Full Source

## `racecar_bt.lisp`

```lisp
--8<-- "examples/pybullet_racecar/bt/racecar_bt.lisp"
```

## Walkthrough

- high-priority safety branch runs first (reactive fallback pattern)
- planner branch uses `plan-action` and writes actions back to blackboard
- runtime script (`run_demo.py`) drives the observe/tick/act loop around this tree
