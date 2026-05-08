# 代码阅读顺序

本文档用于快速理解当前项目的主链路、目录职责和关键代码入口。建议按顺序阅读，不要从 ROS2 节点或模型脚本直接跳进细节。

## 1. 先看整体链路

先读根目录 `README.md`，明确系统边界：

```text
手机 IP Webcam / RTSP
  -> network_camera_node
  -> /camera/image_raw
  -> ros2_tcp_client_node
  -> image_server
  -> depth_server.py
  -> ObstacleRiskAnalyzer
  -> /image_service/depth_image, /obstacle_result, /cmd_vel
```

重点理解两个事实：

- ROS2 负责节点编排、话题发布、参数管理和控制输出。
- C++ Muduo 服务负责图像请求处理，Python 只承载 Depth Anything V2 推理。

## 2. 看配置和启动入口

先看这些文件，理解项目如何启动：

```text
config/image_server.yaml
ros2_ws/src/image_publisher_pkg/config/network_camera_obstacle_avoidance.yaml
ros2_ws/src/image_publisher_pkg/launch/network_camera_obstacle_avoidance_launch.py
scripts/run_network_camera_pipeline.sh
scripts/run_depth_server.sh
scripts/run_image_server.sh
```

参数优先级是：

```text
C++ declare_parameter 默认值
  -> YAML 配置文件
  -> launch / shell 环境变量启动覆盖
  -> ros2 param set 运行时动态修改
```

阅读重点：

- YAML 是默认部署配置。
- `config/image_server.yaml` 控制 `image_server` 端口、Depth 服务地址、障碍物检测 ROI 和风险阈值。
- launch 负责启动三个 ROS2 节点。
- shell 脚本负责 source ROS 环境、检查路径、规避 Conda 对 ROS2 Python 的影响。

## 3. 看基础协议和数据格式

再读公共 C++ 库：

```text
include/realtime_image_service/protocol.hpp
src/common/protocol.cpp

include/realtime_image_service/result_payload.hpp
src/common/result_payload.cpp

include/realtime_image_service/image_codec.hpp
src/common/image_codec.cpp
```

阅读目标：

- `protocol` 定义 TCP 包头、消息类型和请求 ID。
- `result_payload` 定义服务端返回的 JSON、结果图、深度图如何打包。
- `image_codec` 负责 OpenCV 图像和 JPEG 字节流互转。

这部分是 ROS2 客户端和 Muduo 服务端之间的通信契约。

## 4. 看模型服务边界

阅读：

```text
scripts/depth_server.py
include/realtime_image_service/depth_estimator.hpp
src/common/depth_estimator.cpp
```

阅读目标：

- `depth_server.py` 加载 Depth Anything V2，输入 JPEG，输出归一化深度图。
- `DepthEstimator` 是 C++ 到 Python 模型服务的持久 TCP 客户端。
- `image_server` 复用同一个 `DepthEstimator` 连接，失败时关闭并自动重连，避免每帧重复建立 TCP 连接。
- C++ 主链路不直接依赖 PyTorch，模型环境和实时通信环境被隔离。

## 5. 看 Muduo 图像服务端

阅读：

```text
include/realtime_image_service/image_tcp_server.hpp
include/realtime_image_service/image_server_config.hpp
src/server/server_main.cpp
src/server/image_server_config.cpp
src/server/image_tcp_server.cpp
```

推荐从 `server_main.cpp` 看启动参数，再看 `ImageTcpServer::onMessage()` 和 `ImageTcpServer::HandlePacket()`。

当前服务端主流程：

```text
DecodeRequestImage()
  -> EstimateDepthForImage()
  -> AnalyzeObstacleRisk()
  -> BuildResponseArtifacts()
  -> LogRequest()
```

阅读重点：

- `onMessage()` 只负责拆 TCP 包和校验消息类型。
- `HandlePacket()` 只表达业务流程。
- 图像解码、深度估计、风险分析、响应构造已经拆成独立函数，便于单独测试和维护。
- `server_main.cpp` 启动时先读取 `config/image_server.yaml`，再用命令行参数覆盖端口和 Depth 服务地址。
- 默认 `DEPTH_RESPONSE_CODEC=jpg` 降低深度响应编码和传输开销；需要无损时可切回 `png`。

## 6. 看障碍物风险算法

阅读：

```text
include/realtime_image_service/obstacle_risk_analyzer.hpp
src/common/obstacle_risk_analyzer.cpp
include/realtime_image_service/result_json.hpp
src/common/result_json.cpp
```

阅读目标：

- `ObstacleRiskAnalyzer::Analyze()` 接收归一化深度图。
- 默认 ROI 选取输入照片或视频帧的下面 50%。
- ROI 比例来自 `ObstacleRiskConfig`，默认由 `config/image_server.yaml` 提供。
- 风险判断基于有效深度比例、近距离比例、连通域面积和 5% 分位深度。
- `result_json` 把风险结果和耗时指标输出成 JSON。

注意：Depth Anything V2 输出相对深度，不是米制距离；这里做的是工程风险判断，不是真实距离测量。

## 7. 看 ROS2 网络摄像头节点

阅读：

```text
ros2_ws/src/image_publisher_pkg/include/image_publisher_pkg/network_camera_node.hpp
ros2_ws/src/image_publisher_pkg/src/network_camera_node.cpp
```

阅读目标：

- `cv::VideoCapture(stream_url)` 读取 HTTP MJPEG 或 RTSP。
- 发布 `/camera/image_raw`。
- 断流后按 `reconnect_interval` 重连。
- 支持运行时动态修改 `stream_url`、`fps`、`image_width`、`image_height` 等参数。

## 8. 看 ROS2 TCP 客户端节点

阅读：

```text
ros2_ws/src/image_publisher_pkg/src/ros2_tcp_client_node.cpp
```

建议按函数顺序理解：

```text
OnImage()
  -> SnapshotLatestFrame()
  -> ProcessFrameWithServer()
  -> PublishProcessingResult()
  -> WriteDebugImages()
```

阅读重点：

- `OnImage()` 只缓存最新帧，不做慢推理。
- 后台线程按 `max_request_fps` 限速取最新帧。
- TCP 客户端复用连接，失败时关闭并等待下次重连。
- 输出 `/image_service/processed_image`、`/image_service/depth_image`、`/obstacle_result`、`/image_service/metrics`、`/image_service/status`。

## 9. 看安全控制节点

阅读：

```text
ros2_ws/src/image_publisher_pkg/src/safety_controller_node.cpp
```

阅读目标：

- 订阅 `/obstacle_result`。
- 解析 `risk_level`。
- 发布 `/cmd_vel`。
- 支持动态修改 `low_speed`、`medium_speed`、`stop_on_unknown`。

## 10. 调试时按这个顺序查

摄像头问题：

```bash
python scripts/test_network_camera_stream.py --url "http://手机IP:端口/video"
ros2 topic hz /camera/image_raw
```

模型问题：

```bash
DEPTH_INPUT_SIZE=224 DEPTH_DEVICE=cuda ./scripts/run_depth_server.sh
```

服务端问题：

```bash
./scripts/run_image_server.sh
ros2 topic echo /image_service/status --once
```

实时性问题：

```bash
ros2 param set /network_camera_node fps 5.0
ros2 param set /ros2_tcp_client_node max_request_fps 1.0
ros2 topic hz /image_service/depth_image
```

## 11. 当前代码质量结论

当前项目已经具备实习项目展示价值：

- 主链路清晰，C++ 和 Python 职责分离。
- ROS2 参数、YAML、launch、运行时动态参数已经打通。
- 图像通信、深度推理、风险分析、控制输出都有明确边界。
- 服务端主流程已拆分，阅读入口更清楚。

仍可继续增强的方向：

- 给 `protocol`、`result_payload`、`ObstacleRiskAnalyzer` 增加单元测试。
- 把风险阈值、ROI 比例进一步参数化。
- 将 Depth Anything V2 进一步迁移到 ONNX/TensorRT 或 Triton。
- 引入 clang-format、clang-tidy 和 CI 构建检查。
