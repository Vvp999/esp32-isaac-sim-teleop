#!/usr/bin/env python3
"""
ESP32 joystick -> ROS 2 /cmd_vel bridge.

Reads lines "J,<x>,<y>,<button>" from the ESP32 over USB serial and publishes
geometry_msgs/Twist on /cmd_vel for the Jetbot in Isaac Sim.

  push stick forward  -> drive forward
  push stick sideways -> turn
  hold the button     -> turbo (2x speed)
  unplug / no data    -> robot stops (safety timeout)

Run (inside C:\\IsaacSim-ros_workspaces\\jazzy_ws):
  pixi run python joystick_bridge.py --port COM5
"""
import argparse
import threading
import time

import rclpy
import serial
from geometry_msgs.msg import Twist
from rclpy.node import Node


class JoystickBridge(Node):
    def __init__(self, port, baud, max_lin, max_ang, invert_x, invert_y):
        super().__init__("esp32_joystick_bridge")
        self.pub = self.create_publisher(Twist, "/cmd_vel", 10)
        self.max_lin = max_lin
        self.max_ang = max_ang
        self.sx = -1.0 if invert_x else 1.0
        self.sy = -1.0 if invert_y else 1.0

        self.x = self.y = 0.0
        self.btn = 0
        self.last_rx = 0.0
        self.lock = threading.Lock()

        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.get_logger().info(f"Listening to ESP32 on {port} @ {baud}")
        threading.Thread(target=self._read_loop, daemon=True).start()

        self.create_timer(0.05, self._publish)  # 20 Hz

    def _read_loop(self):
        while rclpy.ok():
            try:
                line = self.ser.readline().decode(errors="ignore").strip()
            except serial.SerialException as e:
                self.get_logger().error(f"Serial error: {e}")
                time.sleep(1.0)
                continue
            if not line:
                continue
            if line.startswith("#"):
                self.get_logger().info(f"ESP32: {line[1:].strip()}")
                continue
            parts = line.split(",")
            if len(parts) != 4 or parts[0] != "J":
                continue
            try:
                x, y, btn = float(parts[1]), float(parts[2]), int(parts[3])
            except ValueError:
                continue
            with self.lock:
                self.x, self.y, self.btn = x, y, btn
                self.last_rx = time.time()

    def _publish(self):
        with self.lock:
            stale = time.time() - self.last_rx > 0.5
            x, y, btn = self.x, self.y, self.btn

        msg = Twist()
        if not stale:
            turbo = 2.0 if btn else 1.0
            # stick forward = linear, stick sideways = turning
            msg.linear.x = self.sy * -y * self.max_lin * turbo
            msg.angular.z = self.sx * -x * self.max_ang * turbo
        self.pub.publish(msg)  # zeros when stale -> robot stops


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True, help="e.g. COM5 (see Arduino IDE > Tools > Port)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--max-lin", type=float, default=0.3, help="m/s at full stick")
    p.add_argument("--max-ang", type=float, default=2.0, help="rad/s at full stick")
    p.add_argument("--invert-x", action="store_true", help="flip turning direction")
    p.add_argument("--invert-y", action="store_true", help="flip forward/backward")
    a = p.parse_args()

    rclpy.init()
    node = JoystickBridge(a.port, a.baud, a.max_lin, a.max_ang, a.invert_x, a.invert_y)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.pub.publish(Twist())  # stop the robot on exit
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
