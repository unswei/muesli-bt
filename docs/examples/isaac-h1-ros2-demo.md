# Isaac Sim / ROS2: H1 locomotion demo

## what this is

This example is the first Isaac-facing H1 locomotion demo for `muesli-bt`.

It keeps the existing thin ROS 2 contract intact:

- `muesli-bt` consumes `nav_msgs/msg/Odometry`
- `muesli-bt` publishes `geometry_msgs/msg/Twist`
- Isaac Sim and NVIDIA's H1 controller handle the low-level humanoid policy path

The checked-in BT chooses between:

- initial stand
- turn-to-heading
- walk segment
- timeout stop
- goal stop

## when to use it

Use this demo when you want to show that:

- the same BT/runtime story can drive a current Isaac Sim setup through ROS 2
- `muesli-bt` can stay as the high-level behaviour and safety layer
- canonical `mbt.evt.v1` logging still applies in an Isaac-backed run

Use a manipulator demo later if you want to prove new host capability bundles.
This example is intentionally not a MoveIt or perception demo.

## how it works

The wiring is split into three layers.

1. `muesli-bt` runs the H1 demo entrypoint through `muslisp_ros2`.
2. The current `ros2` backend observes `/h1_01/odom` and publishes `/h1_01/cmd_vel`.
3. Isaac Sim publishes odometry, joint state, and IMU data, while NVIDIA's `h1_fullbody_controller` node consumes `cmd_vel` and produces `joint_command`.

The Isaac-side topic contract is checked in at:

```yaml
--8<-- "examples/isaac_h1_ros2_demo/isaac/topic_contract.yaml"
```

## api / syntax

Main entrypoint:

- `main.lisp`

Reusable runtime library:

- `demo_runtime.lisp`

Checked-in BT:

- `bt_h1_demo.lisp`

Container helper commands:

- `install-isaac-h1-policy`
- `isaac-h1-policy`
- `verify-isaac-h1-topics`
- `isaac-sim-state`
- `isaac-h1-demo`

## example

Build and enter the stack:

```bash
./tools/docker/isaac_lab_vla_stack/run.sh bash
install-isaacsim
install-isaac-h1-policy
```

Start the Isaac-side H1 controller:

```bash
isaac-h1-policy
```

Once the stage is running and the ROS topics are present:

```bash
verify-isaac-h1-topics
isaac-sim-state play
isaac-h1-demo
```

Expected artefacts:

- run-loop log: `logs/isaac_h1_demo.run-loop.jsonl`
- canonical event log: `logs/isaac_h1_demo.mbt.evt.v1.jsonl`

## gotchas

- This is a Linux NVIDIA workflow. It is not a practical runtime target for macOS.
- The example assumes the Isaac stage publishes `/h1_01/odom` and the H1 policy flow uses `/h1_01/cmd_vel`, `/h1_01/joint_states`, `/h1_01/imu`, and `/h1_01/joint_command`.
- The H1 demo is not on the critical CI path. Treat it as a documented showcase flow.
- `main.lisp` keeps the default `/h1_01` namespace. If you need a different namespace, load `demo_runtime.lisp` and override `topic_ns` in the config map before calling `run-h1-demo`.
- This demo does not add a manipulator contract, MoveIt integration, or perception bundle.

## see also

- [H1 demo full source](isaac-h1-ros2-demo-source.md)
- [ROS2 tutorial](../integration/ros2-tutorial.md)
- [ROS2 backend scope](../integration/ros2-backend-scope.md)
- [muesli-bt Isaac Lab + VLA image](https://github.com/unswei/muesli-bt/blob/main/tools/docker/isaac_lab_vla_stack/README.md)
- [Isaac Lab environment list](https://isaac-sim.github.io/IsaacLab/release/2.2.0/source/overview/environments)
- [Isaac Sim ROS 2 RL controller tutorial](https://docs.isaacsim.omniverse.nvidia.com/latest/ros2_tutorials/tutorial_ros2_rl_controller.html)
