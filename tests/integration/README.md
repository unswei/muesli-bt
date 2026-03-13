# integration tests (l1/l2)

This directory is reserved for heavier conformance tests that rely on simulator or ROS 2 stacks.

- L1: simulator-backed conformance (for example PyBullet or Webots).
- L2: ROS 2/rosbag-backed conformance. The first automated replay case now lives in `tests/conformance/test_conformance_l2_rosbag_main.cpp`.
