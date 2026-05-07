# ROS2 Monocular Depth Obstacle Avoidance

基于本地 USB 摄像头、ROS2、Muduo TCP、OpenCV 和 Depth Anything V2 的单目实时深度估计与障碍物风险判断项目。

这个项目的目标不是做一个离线 demo，而是做成一个接近工程系统的实时链路：

```text
USB Camera
  -> ROS2 /camera/image_raw
  -> ROS2 TCP Client
  -> Muduo image_server
  -> Python Depth Anything V2 inference server
  -> depth map + obstacle risk analysis
  -> ROS2 processed image / depth image / JSON result / safety cmd_vel
```

## 项目亮点

- C++17 + Muduo 实现自定义 TCP 图像服务，支持 JPEG 图像请求和 JSON + 多图像结果返回。
- ROS2 Humble 节点接入 USB 摄像头，发布处理结果图、实时深度图、风险 JSON、系统状态和安全速度指令。
- 接入 Depth Anything V2，支持 CUDA、fp16 autocast、warmup、可调 `input_size`。
- ROS 客户端采用“最新帧缓存 + 后台限速推理 + TCP 连接复用”，避免慢推理阻塞摄像头回调。
- 支持低成本 WSL2/USB 摄像头环境，默认使用 `160x120 + mjpeg2rgb` 保证链路稳定。
- 输出可视化结果：
  - `/image_service/processed_image`: 原图 + ROI + risk/action overlay
  - `/image_service/depth_image`: 彩色深度图 + ROI + risk/action overlay
  - `/obstacle_result`: 障碍物风险 JSON
  - `/image_service/status`: 服务状态和处理 FPS
  - `/cmd_vel`: 安全控制指令

## 技术栈

- ROS2 Humble
- C++17
- Muduo
- OpenCV
- Python / PyTorch
- Depth Anything V2
- CUDA
- cv_bridge / sensor_msgs / geometry_msgs

## 构建

先构建 C++ 服务端：

```bash
cd /home/wzq/MyProject/realtime_image_service
cmake --build build -j$(nproc)
```

再构建 ROS2 包：

```bash
cd /home/wzq/MyProject/realtime_image_service/ros2_ws
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select image_publisher_pkg
```

## 启动

终端 1：启动 Depth Anything V2 推理服务。

```bash
cd /home/wzq/MyProject/realtime_image_service
DEPTH_INPUT_SIZE=280 DEPTH_DEVICE=cuda ./scripts/run_depth_server.sh
```

终端 2：启动 Muduo 图像服务。

```bash
cd /home/wzq/MyProject/realtime_image_service
./scripts/run_image_server.sh
```

终端 3：启动摄像头和 ROS2 实时管线。

```bash
cd /home/wzq/MyProject/realtime_image_service
MAX_REQUEST_FPS=2.0 ./scripts/run_ros_pipeline.sh
```

如果没有启动 Depth Anything V2，也可以用伪深度后端验证 ROS/TCP 链路：

```bash
NO_DEPTH_SERVER=1 ./scripts/run_image_server.sh
```

## 查看结果

查看图像：

```bash
ros2 run rqt_image_view rqt_image_view
```

选择：

```text
/image_service/processed_image
/image_service/depth_image
```

查看 JSON 风险结果：

```bash
ros2 topic echo /obstacle_result --once
```

查看系统状态：

```bash
ros2 topic echo /image_service/status --once
```

查看实时处理 FPS：

```bash
ros2 topic hz /image_service/processed_image
ros2 topic hz /image_service/depth_image
```

最新图片也会写到：

```text
/tmp/ros2_camera_result.jpg
/tmp/ros2_camera_depth.jpg
```

## 性能调参

优先调这两个参数：

```bash
DEPTH_INPUT_SIZE=224 ./scripts/run_depth_server.sh
MAX_REQUEST_FPS=3.0 ./scripts/run_ros_pipeline.sh
```

经验值：

- `DEPTH_INPUT_SIZE=224`: 更快，深度边缘更粗。
- `DEPTH_INPUT_SIZE=280`: 推荐默认值，速度和效果相对平衡。
- `DEPTH_INPUT_SIZE=518`: 效果更细，但实时性差。
- `MAX_REQUEST_FPS=2.0`: 低配 GPU / WSL2 推荐。
- `MAX_REQUEST_FPS=3.0~5.0`: GPU 余量足够时再尝试。

## ROS 话题

| Topic | Type | Description |
| --- | --- | --- |
| `/camera/image_raw` | `sensor_msgs/msg/Image` | 摄像头原始图像 |
| `/image_service/processed_image` | `sensor_msgs/msg/Image` | 原图叠加障碍物结果 |
| `/image_service/depth_image` | `sensor_msgs/msg/Image` | 彩色深度图可视化 |
| `/obstacle_result` | `std_msgs/msg/String` | 风险判断 JSON |
| `/image_service/metrics` | `std_msgs/msg/String` | 服务端耗时指标 |
| `/image_service/status` | `std_msgs/msg/String` | 系统状态 |
| `/cmd_vel` | `geometry_msgs/msg/Twist` | 安全控制速度指令 |

## 简历写法

可以写成：

> 基于 ROS2 + Muduo + Depth Anything V2 实现单目摄像头实时障碍物风险感知系统。使用 C++17 构建 TCP 图像服务和 ROS2 客户端，Python/PyTorch 部署深度估计模型，设计最新帧缓存、后台限速推理、连接复用和状态监控机制，输出实时深度图、风险 JSON 与安全控制指令。

重点强调：

- 自己做了跨语言服务拆分：C++ 实时通信与控制，Python 模型推理。
- 自己做了实时性处理：不阻塞 ROS 回调，慢推理时丢旧帧保最新帧。
- 自己做了工程可观测性：metrics/status/topic hz/debug image。
- 自己知道单目深度的边界：相对深度，不直接等价真实米制距离。

## 当前边界

- Depth Anything V2 输出的是相对深度，不能直接当作真实距离。
- USB 摄像头在 WSL2 下高分辨率 YUYV 可能不稳定，默认使用 MJPEG 低分辨率。
- 当前目标是稳定实时展示和风险判断，不是自动驾驶级避障系统。
- 企业级进一步优化方向：ONNX/TensorRT、模型服务持久连接池、相机标定、实测数据集评估。
