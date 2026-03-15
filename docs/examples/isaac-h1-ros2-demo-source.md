# Isaac Sim / ROS2: H1 demo full source

## `bt_h1_demo.lisp`

```lisp
--8<-- "examples/isaac_h1_ros2_demo/lisp/bt_h1_demo.lisp"
```

## `demo_runtime.lisp`

```lisp
--8<-- "examples/isaac_h1_ros2_demo/lisp/demo_runtime.lisp"
```

## walkthrough

- the BT stays intentionally small and only arbitrates high-level modes
- the runtime script keeps waypoint bookkeeping and timeout detection in ordinary Lisp
- `env.run-loop` still owns the `observe -> on_tick -> act -> step` control loop
- the Isaac-facing contract stays on the existing ROS 2 `Odometry -> Twist` path

## topic contract

```yaml
--8<-- "examples/isaac_h1_ros2_demo/isaac/topic_contract.yaml"
```
