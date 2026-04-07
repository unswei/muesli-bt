# PyBullet Racecar: Full Source

## `racecar_bt.lisp`

```lisp
--8<-- "examples/pybullet_racecar/bt/racecar_bt.lisp"
```

## Walkthrough

- high-priority safety branch runs first (reactive fallback pattern)
- planner branch uses `plan-action` and writes actions back to blackboard
- runtime script (`run_demo.py`) drives the observe/tick/act loop around this tree

## `flagship_entry.lisp`

```lisp
--8<-- "examples/pybullet_racecar/bt/flagship_entry.lisp"
```

## Flagship Notes

- this mirrors the shared v0.5 flagship branch order
- PyBullet derives shared keys before the BT tick
- the runtime projects `[linear_x, angular_z]` to steering/throttle after the BT tick
