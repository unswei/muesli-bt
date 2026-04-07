# Isaac Sim: TurtleBot3 ROS2 Demo

## what this is

This example shows a TurtleBot3 Burger in Isaac Sim driven through the existing ROS 2 wheeled interface.

`muesli-bt` reads `nav_msgs/msg/Odometry`, publishes `geometry_msgs/msg/Twist`, and runs the same flagship goal-seeking behaviour used by the other wheeled examples.

The demo is designed to be easy to stage, easy to record, and easy to repeat.

## when to use it

Use this example when you want to:

- run the wheeled flagship in Isaac Sim
- keep the ROS 2 interface simple and familiar
- capture a short simulator clip for docs or the website
- collect the same JSONL and canonical event logs as the other ROS-backed runs

## how it works

The runtime boundary is:

```text
/robot/odom     nav_msgs/msg/Odometry
/robot/cmd_vel  geometry_msgs/msg/Twist
```

Isaac Sim hosts the robot, scene, camera, and ROS 2 bridge.
`muesli-bt` runs the existing ROS 2 flagship entrypoint:

```bash
source /opt/ros/humble/setup.bash
./build/linux-ros2/muslisp_ros2 examples/repl_scripts/ros2-flagship-goal.lisp
```

The checked-in Isaac-side contract is:

```yaml
--8<-- "examples/isaac_wheeled_ros2_demo/isaac/topic_contract.yaml"
```

The checked-in TurtleBot3 scene recipe is:

```yaml
--8<-- "examples/isaac_wheeled_ros2_demo/isaac/turtlebot3_scene_recipe.yaml"
```

## api / syntax

### files

Key files for this demo:

- `examples/repl_scripts/ros2-flagship-goal.lisp`
- `examples/isaac_wheeled_ros2_demo/isaac/topic_contract.yaml`
- `examples/isaac_wheeled_ros2_demo/isaac/turtlebot3_scene_recipe.yaml`

### topic contract

Required topics:

```text
/robot/odom     nav_msgs/msg/Odometry
/robot/cmd_vel  geometry_msgs/msg/Twist
```

Useful companion topics:

```text
/tf
/clock
```

### scene layout

The intended scene is deliberately compact:

- one TurtleBot3 Burger mobile base
- one visible goal marker
- two or three fixed obstacles
- one third-person camera angle with the full route in view

This keeps the run legible in a short clip and keeps the ROS 2 setup straightforward.

## example

### run it

Build the ROS 2 runner if needed:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/linux-ros2 -j
```

Start Isaac Sim and prepare the scene:

```bash
source /opt/ros/humble/setup.bash
# load Isaac/Environments/Simple_Room/simple_room.usd
# import turtlebot3/turtlebot3_description/urdf/turtlebot3_burger.urdf
# apply examples/isaac_wheeled_ros2_demo/isaac/turtlebot3_scene_recipe.yaml
# enable the ROS 2 bridge
# press play once /robot/odom and /robot/cmd_vel are wired
```

Verify the bridge:

```bash
source /opt/ros/humble/setup.bash
ros2 topic list | grep -E '^/robot/(odom|cmd_vel)$'
ros2 topic echo /robot/odom --once
ros2 topic echo /tf --once
```

Run the demo:

```bash
source /opt/ros/humble/setup.bash
./build/linux-ros2/muslisp_ros2 examples/repl_scripts/ros2-flagship-goal.lisp
```

If you are using the checked-in Isaac Lab container helpers:

```bash
./tools/docker/isaac_lab_vla_stack/run.sh verify-isaac-wheeled-topics
./tools/docker/isaac_lab_vla_stack/run.sh isaac-sim-state play
./tools/docker/isaac_lab_vla_stack/run.sh isaac-wheeled-demo
```

### what to look for

- the robot should turn cleanly towards the route and progress to the goal without changing the ROS 2 message surface
- `/robot/odom` should stay live throughout the run
- `/robot/cmd_vel` should reflect the active branch decisions from the flagship behaviour
- the run should produce both the run-loop log and the canonical event log

### logs

The current runner writes:

```text
build/linux-ros2/ros2-flagship-goal.jsonl
build/linux-ros2/ros2-flagship-goal/events.jsonl
```

These logs can be inspected directly or normalised with:

```bash
python3 examples/flagship_wheeled/tools/normalise_run.py \
  --backend ros2 \
  --input build/linux-ros2/ros2-flagship-goal.jsonl
```

### capture it

For a clean website asset:

1. frame the third-person camera before pressing play
2. hide overlays where practical
3. record one short start-to-goal run
4. export one screenshot from the same angle
5. keep the media beside the matching run logs

Recommended output set:

- `docs/assets/demos/isaac-turtlebot3/showcase.mp4`
- `docs/assets/demos/isaac-turtlebot3/scene.png`
- `build/linux-ros2/ros2-flagship-goal.jsonl`
- `build/linux-ros2/ros2-flagship-goal/events.jsonl`

Suggested caption:

`TurtleBot3 Burger in Isaac Sim running the shared muesli-bt wheeled demo through the ROS 2 odometry-to-twist interface.`

## gotchas

- Keep the robot namespace on `/robot` unless you are also updating the demo configuration.
- Keep the camera angle wide enough to show both the start area and the goal.
- If a particular Isaac build does not expose a working TurtleBot3 asset, keep the same ROS 2 contract and swap only the robot asset.
- If the scene publishes different topic names, document the remap explicitly before recording media.

## see also

- `examples/isaac_wheeled_ros2_demo/isaac/topic_contract.yaml`
- `examples/isaac_wheeled_ros2_demo/isaac/turtlebot3_scene_recipe.yaml`
- [Isaac Sim / ROS2: H1 locomotion demo](isaac-h1-ros2-demo.md)
- [ROS2 tutorial](../integration/ros2-tutorial.md)
- [cross-transport comparison protocol](../integration/cross-transport-comparison-protocol.md)
