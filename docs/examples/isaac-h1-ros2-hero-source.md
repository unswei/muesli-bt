# Isaac Sim / ROS2: H1 hero full source

## `bt_h1_hero.lisp`

```lisp
--8<-- "examples/isaac_h1_ros2_hero/lisp/bt_h1_hero.lisp"
```

## `hero_runtime.lisp`

```lisp
--8<-- "examples/isaac_h1_ros2_hero/lisp/hero_runtime.lisp"
```

## walkthrough

- the BT stays intentionally small and only arbitrates high-level modes
- the runtime script keeps waypoint bookkeeping and timeout detection in ordinary Lisp
- `env.run-loop` still owns the `observe -> on_tick -> act -> step` control loop
- the Isaac-facing contract stays on the existing ROS 2 `Odometry -> Twist` path

## topic contract

```yaml
--8<-- "examples/isaac_h1_ros2_hero/isaac/topic_contract.yaml"
```
