#!/usr/bin/env python3
"""Bridge localhost UDP state/control packets onto the ROS 2 odom/cmd_vel surface."""

from __future__ import annotations

import argparse
import json
import math
import socket
import time
from typing import Optional

import rclpy
from geometry_msgs.msg import Quaternion, TransformStamped, Twist
from nav_msgs.msg import Odometry
from rclpy.node import Node
from tf2_msgs.msg import TFMessage


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--state-port", type=int, default=37020)
    parser.add_argument("--command-port", type=int, default=37021)
    parser.add_argument("--topic-ns", default="/robot")
    parser.add_argument("--odom-frame", default="odom")
    parser.add_argument("--base-frame", default="base_link")
    parser.add_argument("--command-timeout-s", type=float, default=0.5)
    parser.add_argument("--poll-sleep-s", type=float, default=0.005)
    return parser.parse_args()


def yaw_to_quaternion(yaw: float) -> Quaternion:
    half = yaw * 0.5
    quat = Quaternion()
    quat.x = 0.0
    quat.y = 0.0
    quat.z = math.sin(half)
    quat.w = math.cos(half)
    return quat


class UdpRos2Bridge(Node):
    def __init__(self, args: argparse.Namespace) -> None:
        super().__init__("muesli_bt_tb3_udp_bridge")
        self._args = args
        self._latest_cmd = {"linear_x": 0.0, "angular_z": 0.0}
        self._latest_cmd_time = time.monotonic()
        self._last_state_time: Optional[float] = None

        topic_root = args.topic_ns.rstrip("/")
        self._cmd_sub = self.create_subscription(Twist, f"{topic_root}/cmd_vel", self._on_cmd_vel, 10)
        self._odom_pub = self.create_publisher(Odometry, f"{topic_root}/odom", 10)
        self._tf_pub = self.create_publisher(TFMessage, "/tf", 10)

        self._state_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._state_sock.bind((args.listen_host, args.state_port))
        self._state_sock.setblocking(False)

        self._command_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._command_target = (args.listen_host, args.command_port)

    def _on_cmd_vel(self, msg: Twist) -> None:
        self._latest_cmd = {
            "linear_x": float(msg.linear.x),
            "angular_z": float(msg.angular.z),
        }
        self._latest_cmd_time = time.monotonic()

    def _current_command(self) -> dict[str, float]:
        if time.monotonic() - self._latest_cmd_time > self._args.command_timeout_s:
            return {"linear_x": 0.0, "angular_z": 0.0}
        return self._latest_cmd

    def _publish_state(self, state: dict) -> None:
        stamp_ns = int(state["t_ms"]) * 1_000_000
        yaw = float(state["yaw"])
        vx = float(state["vx"])
        vy = float(state["vy"])
        quat = yaw_to_quaternion(yaw)

        odom = Odometry()
        odom.header.stamp.sec = stamp_ns // 1_000_000_000
        odom.header.stamp.nanosec = stamp_ns % 1_000_000_000
        odom.header.frame_id = self._args.odom_frame
        odom.child_frame_id = self._args.base_frame
        odom.pose.pose.position.x = float(state["pose_x"])
        odom.pose.pose.position.y = float(state["pose_y"])
        odom.pose.pose.position.z = 0.0
        odom.pose.pose.orientation = quat
        odom.twist.twist.linear.x = vx
        odom.twist.twist.linear.y = vy
        odom.twist.twist.angular.z = float(state["wz"])
        self._odom_pub.publish(odom)

        transform = TransformStamped()
        transform.header.stamp = odom.header.stamp
        transform.header.frame_id = self._args.odom_frame
        transform.child_frame_id = self._args.base_frame
        transform.transform.translation.x = odom.pose.pose.position.x
        transform.transform.translation.y = odom.pose.pose.position.y
        transform.transform.translation.z = 0.0
        transform.transform.rotation = quat
        self._tf_pub.publish(TFMessage(transforms=[transform]))

    def spin_bridge(self) -> None:
        try:
            while rclpy.ok():
                rclpy.spin_once(self, timeout_sec=0.0)
                state_seen = False
                while True:
                    try:
                        payload, _ = self._state_sock.recvfrom(65536)
                    except BlockingIOError:
                        break
                    state_seen = True
                    self._last_state_time = time.monotonic()
                    state = json.loads(payload.decode("utf-8"))
                    self._publish_state(state)

                self._command_sock.sendto(
                    json.dumps(self._current_command()).encode("utf-8"),
                    self._command_target,
                )

                if not state_seen:
                    time.sleep(self._args.poll_sleep_s)
        finally:
            self._state_sock.close()
            self._command_sock.close()


def main() -> None:
    args = parse_args()
    rclpy.init()
    node = UdpRos2Bridge(args)
    try:
        node.spin_bridge()
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
