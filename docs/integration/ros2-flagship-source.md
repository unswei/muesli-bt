# ros2 flagship source

## `ros2-flagship-goal.lisp`

```lisp
--8<-- "examples/repl_scripts/ros2-flagship-goal.lisp"
```

## walkthrough

- attaches the released `ros2` backend
- keeps the transport on `nav_msgs/msg/Odometry` in and `geometry_msgs/msg/Twist` out
- derives the shared wheeled flagship contract from odometry plus fixed goal and obstacle geometry
- ticks the shared BT from `examples/flagship_wheeled/lisp/bt_goal_flagship.lisp`
- writes both the flagship run log and the canonical event log

## see also

- [ros2 tutorial](ros2-tutorial.md)
- [cross-transport flagship for v0.5](cross-transport-flagship.md)
