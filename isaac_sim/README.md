# Isaac Sim scene setup

Tested with **Isaac Sim 6.2.0** on Windows, launched through `pixi run sim` so the ROS 2 bridge is enabled.

## 1. Scene
1. **Create → Physics → Ground Plane**
2. Content browser → search `jetbot` → drag `jetbot.usd` into the viewport (prim path `/World/jetbot`)
3. Save the stage (e.g. `jetbot_ros.usd`). You can put your saved `.usd` in this folder.

## 2. Generate the drive controller
**Tools → Robotics → OmniGraph Controllers → Differential Controller**

| Field | Value |
|---|---|
| Robot Prim | `/World/jetbot` |
| wheel radius | `0.03` |
| distance between wheels | `0.1125` |
| Use Keyboard Control (WASD) | **off** |

This creates `/Graphs/differential_controller` with *On Playback Tick → Differential Controller → Articulation Controller*.

## 3. Add the ROS 2 input
In **Window → Graph Editors → Action Graph**, open the graph and add **ROS2 Context**, **ROS2 Subscribe Twist** (topic `cmd_vel`) and two **Break 3-Vector** nodes:

| From | To |
|---|---|
| On Playback Tick · Tick | ROS2 Subscribe Twist · Exec In |
| ROS2 Context · Context | ROS2 Subscribe Twist · Context |
| Subscribe Twist · Linear Velocity | Break 3-Vector #1 · Tuple |
| Break 3-Vector #1 · X | Differential Controller · Desired Linear Velocity |
| Subscribe Twist · Angular Velocity | Break 3-Vector #2 · Tuple |
| Break 3-Vector #2 · Z | Differential Controller · Desired Angular Velocity |

Keep *On Playback Tick → Differential Controller* and *→ Articulation Controller* connected.

## 4. Verify
Press **Play**, then:
```bat
pixi run ros2 topic info /cmd_vel      :: Subscription count: 1
pixi run ros2 topic pub /cmd_vel geometry_msgs/msg/Twist "{linear: {x: 0.2}, angular: {z: 0.5}}"
```
The Jetbot drives in a circle.
