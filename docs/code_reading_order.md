# Code Reading Order

这个项目现在定位为 ROS2 实时单目深度感知与可视化系统。建议按运行链路阅读：

```text
network_camera_node
  -> ros2_tcp_client_node
  -> TcpImageClient
  -> Muduo image_server
  -> DepthEstimator
  -> depth_server.py
  -> /image_service/processed_image
  -> /image_service/depth_image
  -> /image_service/result
  -> /image_service/metrics
  -> /image_service/status
```

## 1. ROS2 Camera Input

文件：

```text
ros2_ws/src/image_publisher_pkg/src/network_camera_node.cpp
ros2_ws/src/image_publisher_pkg/include/image_publisher_pkg/network_camera_node.hpp
```

重点看：

- `cv::VideoCapture(stream_url)` 读取 HTTP MJPEG 或 RTSP。
- 发布 `/camera/image_raw`。
- 支持 `fps`、`image_width`、`image_height`、`reconnect_interval` 参数。
- 断流后按间隔重连，避免忙等。

## 2. ROS2 TCP Client Node

文件：

```text
ros2_ws/src/image_publisher_pkg/src/ros2_tcp_client_node.cpp
```

重点看：

- 订阅 `/camera/image_raw`。
- 只缓存最新帧，避免模型慢时堆积旧图。
- 后台线程按 `max_request_fps` 请求 C++ 图像服务。
- 发布：
  - `/image_service/processed_image`
  - `/image_service/depth_image`
  - `/image_service/result`
  - `/image_service/metrics`
  - `/image_service/status`

## 3. TCP Client And Protocol

文件：

```text
include/realtime_image_service/tcp_image_client.hpp
src/client/tcp_image_client.cpp
include/realtime_image_service/protocol.hpp
src/common/protocol.cpp
src/common/result_payload.cpp
```

重点看：

- 自定义包头：magic、version、msg_type、payload_size、request_id。
- TCP 是字节流，所以发送和接收都要处理半包/粘包。
- 响应 payload 包含 JSON、处理图、深度图。
- 客户端复用 TCP 连接，减少每帧 connect/close 开销。

## 4. Muduo Image Server

文件：

```text
include/realtime_image_service/image_tcp_server.hpp
src/server/image_tcp_server.cpp
src/server/server_main.cpp
src/server/image_server_config.cpp
```

重点看：

- Muduo 负责 TCP 连接和消息回调。
- 服务端流程：解码图像 -> 调用深度服务 -> 渲染深度伪彩色图 -> 打包 JSON/图像。
- `BuildDepthVisualization` 把归一化深度转成 Turbo colormap。
- `BuildMetricsLog` 输出 decode、depth、encode、total 耗时。

## 5. Python Depth Service

文件：

```text
scripts/depth_server.py
scripts/run_depth_server.sh
scripts/export_depth_anything_onnx.py
scripts/benchmark_depth_server.py
```

重点看：

- `depth_server.py` 是独立 TCP 推理服务。
- 支持 `pytorch` 和 `onnxruntime` 后端。
- 默认部署使用 `CUDAExecutionProvider`，CUDA 不可用时直接报错。
- `benchmark_depth_server.py` 用于端到端压测推理延迟。

## 6. Launch And Config

文件：

```text
config/image_server.yaml
ros2_ws/src/image_publisher_pkg/config/network_camera_depth_visualization.yaml
ros2_ws/src/image_publisher_pkg/launch/network_camera_depth_visualization_launch.py
scripts/run_image_server.sh
scripts/run_network_camera_pipeline.sh
```

重点看：

- `image_server.yaml` 配置 C++ 图像服务端口和 Python depth_server 地址。
- ROS2 YAML 配置相机 URL、图像尺寸、服务端地址、最大请求 FPS。
- launch 文件同时启动 `network_camera_node` 和 `ros2_tcp_client_node`。

## 7. Main Interview Points

- ROS2 C++ 节点、topic、launch、YAML 参数。
- C++/Python 模型服务解耦。
- Muduo TCP 服务端和自定义二进制协议。
- 最新帧缓存和推理限频，避免实时链路积压。
- ONNX Runtime CUDA 部署和 FPS/延迟统计。
- Depth Anything V2 输出相对深度，系统重点是实时深度可视化，不声称输出真实米制距离。
