# nav2 real-stack evidence scenario

This directory contains the reviewed defaults for capturing real Nav2 stack evidence for the experimental `wheeled-goal-flagship-nav-capability` variant.

The scenario file is:

```json
--8<-- "examples/nav2_real_stack/wheeled_flagship_scenario.json"
```

Run the capture on a ROS2 Humble machine after starting a Nav2 stack that exposes `/navigate_to_pose`:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF
cmake --build build/linux-ros2 -j --target wheeled_flagship_nav2_real_stack_evidence

python3 tools/run_wheeled_flagship_nav2_real_stack_evidence.py \
  --helper build/linux-ros2/wheeled_flagship_nav2_real_stack_evidence \
  --write \
  --ros-distro humble \
  --simulator external-nav2-stack \
  --goal-x 1.0 \
  --goal-y 0.0 \
  --goal-yaw 0.0 \
  --process-timeout-s 120
```

The capture writes artefacts under `fixtures/ros2/wheeled_flagship_nav2_real_stack/`.

This example does not launch Nav2, a map server, a lifecycle manager, or a simulator. Those remain external to the evidence helper so the helper only proves the `muesli-bt` side of the action-client boundary.
