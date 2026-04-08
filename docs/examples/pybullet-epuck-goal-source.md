# PyBullet e-puck-style Goal: Full Source

## `flagship_entry.lisp`

```lisp
--8<-- "examples/pybullet_epuck_goal/bt/flagship_entry.lisp"
```

## `run_demo.py`

```python
--8<-- "examples/pybullet_epuck_goal/run_demo.py"
```

## Walkthrough

- the checked-in BT file stays aligned with the shared wheeled flagship shape
- the Python runner derives the shared blackboard keys from PyBullet pose and ray observations
- BT output stays as shared `[linear_x, angular_z]`
- the simulator converts that shared command into left and right wheel speeds for the differential-drive surrogate
