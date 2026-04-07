# Isaac wheeled ROS2 demo assets

This directory contains the Isaac-side assets for the TurtleBot3 ROS 2 wheeled demo.

Files:

- `isaac/topic_contract.yaml`: required ROS 2 topics, frames, and bridge expectations
- `isaac/turtlebot3_scene_recipe.yaml`: TurtleBot3 Burger scene layout, controller values, and camera framing

## how to use these files

1. Build the ROS 2 runner and prepare `examples/repl_scripts/ros2-flagship-goal.lisp`.
2. Open Isaac Sim and load `Isaac/Environments/Simple_Room/simple_room.usd`.
3. Import the TurtleBot3 Burger URDF described in `isaac/turtlebot3_scene_recipe.yaml`.
4. Configure the ROS 2 bridge and OmniGraph so Isaac publishes `/robot/odom` and subscribes to `/robot/cmd_vel`.
5. Verify `/robot/odom` and `/tf`.
6. Run `./build/linux-ros2/muslisp_ros2 examples/repl_scripts/ros2-flagship-goal.lisp`.
7. Record a short clip and export one screenshot from the configured third-person camera.

## runtime shape

- Isaac Sim provides the robot, scene, and ROS 2 bridge
- `muesli-bt` runs `examples/repl_scripts/ros2-flagship-goal.lisp`
- the public interface stays on `nav_msgs/msg/Odometry` in and `geometry_msgs/msg/Twist` out

## see also

- `docs/examples/isaac-wheeled-ros2-showcase.md`
- `docs/integration/ros2-tutorial.md`
