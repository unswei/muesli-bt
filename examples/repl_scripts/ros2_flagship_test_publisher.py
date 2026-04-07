#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math

import rclpy
from geometry_msgs.msg import Quaternion
from nav_msgs.msg import Odometry
from rclpy.node import Node


def yaw_to_quaternion(yaw: float) -> Quaternion:
    q = Quaternion()
    q.z = math.sin(yaw * 0.5)
    q.w = math.cos(yaw * 0.5)
    return q


class FlagshipTestPublisher(Node):
    def __init__(self, topic: str, tick_hz: float, duration_sec: float, goal_x: float, goal_y: float) -> None:
        super().__init__("ros2_flagship_test_publisher")
        self._publisher = self.create_publisher(Odometry, topic, 10)
        self._tick_hz = tick_hz
        self._goal_x = goal_x
        self._goal_y = goal_y
        self._steps_total = max(1, int(round(duration_sec * tick_hz)))
        self._step_index = 0
        self._timer = self.create_timer(1.0 / tick_hz, self._publish_step)

    def _publish_step(self) -> None:
        progress = min(1.0, float(self._step_index) / float(self._steps_total))
        x = self._goal_x * progress
        y = self._goal_y * progress
        vx = self._goal_x / max(1.0, float(self._steps_total) / self._tick_hz)
        vy = self._goal_y / max(1.0, float(self._steps_total) / self._tick_hz)

        msg = Odometry()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = "map"
        msg.child_frame_id = "base_link"
        msg.pose.pose.position.x = x
        msg.pose.pose.position.y = y
        msg.pose.pose.orientation = yaw_to_quaternion(0.0)
        msg.twist.twist.linear.x = vx if progress < 1.0 else 0.0
        msg.twist.twist.linear.y = vy if progress < 1.0 else 0.0
        msg.twist.twist.angular.z = 0.0
        self._publisher.publish(msg)

        self._step_index += 1
        if self._step_index > self._steps_total:
            self._timer.cancel()
            self.get_logger().info("flagship odometry trajectory complete")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Publish a simple ROS2 odometry trajectory for the flagship wrapper.")
    parser.add_argument("--topic", default="/robot/odom", help="Odometry topic to publish to.")
    parser.add_argument("--tick-hz", type=float, default=20.0, help="Publishing rate in Hz.")
    parser.add_argument("--duration-sec", type=float, default=6.0, help="Trajectory duration in seconds.")
    parser.add_argument("--goal-x", type=float, default=1.0, help="Goal x position in metres.")
    parser.add_argument("--goal-y", type=float, default=0.0, help="Goal y position in metres.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rclpy.init()
    node = FlagshipTestPublisher(args.topic, args.tick_hz, args.duration_sec, args.goal_x, args.goal_y)
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
