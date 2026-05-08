# ROS2 Network Camera Monocular Obstacle Avoidance

基于手机 IP Webcam、ROS2 Humble、C++17、Muduo TCP、OpenCV 和 Depth Anything V2 的单目实时深度估计与障碍物风险感知项目。

当前主链路：

```text
Phone IP Webcam / RTSP stream
  -> C++ network_camera_node publishes /camera/image_raw
  -> C++ ros2_tcp_client_node sends latest frames to image_server
  -> C++ Muduo image_server calls Python Depth Anything V2 service
  -> C++ obstacle risk analysis
  -> ROS2 processed image / depth image / JSON result / cmd_vel
```

## Project Scope

这个仓库只保留当前任务相关代码：

- 网络摄像头接入：`network_camera_node`
- ROS2 到 Muduo 的 TCP 图像客户端：`ros2_tcp_client_node`
- Muduo 图像服务端：`image_server`
- Depth Anything V2 Python 推理服务：`scripts/depth_server.py`
- 障碍物风险分析和安全速度输出：`safety_controller_node`

已移除早期实验代码：

- 文件图片发布 demo
- USB `/dev/video0` / `usb_cam` launch
- Sobel/CUDA 边缘检测实验代码
- 独立命令行 `image_client` demo

## Project Layout

```text
include/realtime_image_service/        C++ 公共头文件
config/                                image_server 和风险检测 YAML 配置
src/common/                            协议、图像编解码、深度客户端、风险分析
src/server/                            Muduo image_server
ros2_ws/src/image_publisher_pkg/       ROS2 C++ 节点、launch、YAML
scripts/                               启动脚本和 Depth Anything V2 服务
models/                                本地 ONNX/TensorRT 模型产物，不提交大文件
docs/                                  阅读顺序和简历说明
```

建议先阅读：

```text
docs/code_reading_order.md
docs/inference_deployment.md
docs/performance_report.md
```

## Third-Party Model

`Depth-Anything-V2-main/` 和模型权重不提交到本仓库，避免把第三方源码和大文件放进项目仓库。

首次运行前在项目根目录准备 Depth Anything V2：

```bash
git clone https://github.com/DepthAnything/Depth-Anything-V2.git Depth-Anything-V2-main
mkdir -p Depth-Anything-V2-main/checkpoints
```

然后把官方 `vits` 权重放到：

```text
Depth-Anything-V2-main/checkpoints/depth_anything_v2_vits.pth
```

## Features

- C++ ROS2 网络摄像头节点使用 `cv::VideoCapture(stream_url)` 读取 HTTP MJPEG 或 RTSP 流。
- ROS2 客户端采用最新帧缓存、后台限速推理和 TCP 连接复用，避免深度模型阻塞摄像头回调。
- C++ Muduo 服务端统一完成图像解码、深度服务调用、风险分析、结果图/深度图编码。
- 障碍物检测 ROI 和风险阈值由 `config/image_server.yaml` 配置，不写死在算法里。
- Python Depth Anything V2 服务支持 CUDA、fp16 autocast、warmup、可调 `input_size` 和持久 TCP 连接。
- 推理服务支持 `pytorch` / `onnxruntime` 后端切换，提供 ONNX 导出和 TCP 压测脚本。
- `image_server` 复用到 `depth_server.py` 的长连接，断线后自动重连，避免每帧重复 `connect/close`。
- 输出可观测话题：结果图、深度图、风险 JSON、metrics、status、`cmd_vel`。

## Build

构建 C++ Muduo 服务端：

```bash
cd /home/wzq/MyProject/realtime_image_service
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

构建 ROS2 包：

```bash
cd /home/wzq/MyProject/realtime_image_service/ros2_ws
conda deactivate  # 如果当前终端显示 (base)，先执行这一行
source /opt/ros/humble/setup.bash
colcon build --packages-select image_publisher_pkg --symlink-install \
  --cmake-args \
  -Dfmt_DIR=/usr/lib/x86_64-linux-gnu/cmake/fmt \
  -DPython3_EXECUTABLE=/usr/bin/python3
source install/setup.bash
```

## Test Phone Stream

手机安装 IP Webcam / IP Camera App，手机和电脑连接同一 Wi-Fi，启动手机端服务后会看到类似地址：

```text
http://192.168.31.89:8080/video
```

先在 WSL 里直接测试 OpenCV 能否读取：

```bash
cd /home/wzq/MyProject/realtime_image_service
python scripts/test_network_camera_stream.py --url "http://手机IP:端口/video"
```

成功时会输出：

```text
opened: True
ret: True
frame shape: (...)
output path: /tmp/network_camera_test.jpg
```

## Run

终端 1：启动 ONNX Runtime CUDA 深度推理服务。

```bash
cd /home/wzq/MyProject/realtime_image_service
export LD_LIBRARY_PATH="$CONDA_PREFIX/lib:$LD_LIBRARY_PATH"

DEPTH_BACKEND=onnxruntime \
DEPTH_ONNX_PATH=./models/depth_anything_v2_vits_280.onnx \
DEPTH_INPUT_SIZE=280 \
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider \
DEPTH_RESPONSE_CODEC=jpg \
./scripts/run_depth_server.sh
```

如果需要回退到 PyTorch 后端：

```bash
DEPTH_BACKEND=pytorch DEPTH_INPUT_SIZE=280 DEPTH_DEVICE=cuda ./scripts/run_depth_server.sh
```

终端 2：启动 Muduo 图像服务。

```bash
cd /home/wzq/MyProject/realtime_image_service
./scripts/run_image_server.sh
```

默认会读取：

```text
config/image_server.yaml
```

需要临时覆盖端口或 Depth 服务地址时再设置环境变量：

```bash
IMAGE_SERVER_PORT=9999 DEPTH_HOST=127.0.0.1 DEPTH_PORT=18080 ./scripts/run_image_server.sh
```

如果要调整检测区域，改这个文件里的：

```yaml
obstacle_risk_analyzer:
  roi_x_ratio: 0.2
  roi_y_ratio: 0.50
  roi_width_ratio: 0.7
  roi_height_ratio: 0.45
```

如果只想验证 ROS/TCP 链路，不跑 Depth Anything：

```bash
cd /home/wzq/MyProject/realtime_image_service
NO_DEPTH_SERVER=1 ./scripts/run_image_server.sh
```

终端 3：启动网络摄像头 ROS2 管线。

```bash
cd /home/wzq/MyProject/realtime_image_service
STREAM_URL="http://手机IP:端口/video" \
IMAGE_WIDTH=320 \
IMAGE_HEIGHT=240 \
FPS=60.0 \
MAX_REQUEST_FPS=60.0 \
OUTPUT_PATH="" \
DEPTH_OUTPUT_PATH="" \
./scripts/run_network_camera_pipeline.sh
```

## View Results

如果使用 ROS2 Python 工具，先退出 conda 环境：

```bash
conda deactivate
source /opt/ros/humble/setup.bash
source /home/wzq/MyProject/realtime_image_service/ros2_ws/install/setup.bash
```

检查输入图像：

```bash
ros2 topic hz /camera/image_raw
```

查看结果图：

```bash
ros2 run rqt_image_view rqt_image_view
```

选择：

```text
/camera/image_raw
/image_service/processed_image
/image_service/depth_image
```

查看风险 JSON：

```bash
ros2 topic echo /obstacle_result --once
```

查看服务状态和 FPS：

```bash
ros2 topic echo /image_service/status --once
ros2 topic hz /image_service/depth_image
```

默认关闭调试图片落盘，避免 60 FPS 下磁盘写 JPEG 影响实时性。需要调试图片时再设置：

```bash
OUTPUT_PATH="/tmp/ros2_camera_result.jpg" \
DEPTH_OUTPUT_PATH="/tmp/ros2_camera_depth.jpg" \
./scripts/run_network_camera_pipeline.sh
```

## Runtime Tuning

默认目标配置：

```bash
DEPTH_INPUT_SIZE=280
IMAGE_WIDTH=320
IMAGE_HEIGHT=240
FPS=60.0
MAX_REQUEST_FPS=60.0
DEPTH_RESPONSE_CODEC=jpg
```

调参方向：

- 60 FPS 默认只使用 ONNX Runtime CUDA 后端；如果 CUDA/cuDNN 不可用，先修复 GPU 环境，不走 CPU fallback。
- 卡顿优先降低手机 App 输出分辨率，而不是只在 ROS 节点里 resize。
- OpenCV 打不开 RTSP 时优先使用 HTTP MJPEG。
- `MAX_REQUEST_FPS` 控制深度推理请求频率，不等于摄像头发布频率。
- 结果图片默认不落盘；需要调试时再设置 `OUTPUT_PATH` 和 `DEPTH_OUTPUT_PATH`。
- `DEPTH_RESPONSE_CODEC=jpg` 优先保证实时性；需要无损深度图时再改成 `png`。
- Depth Anything V2 是相对深度模型，不输出真实米制距离。

## Inference Deployment

完整推理部署说明见：

```text
docs/inference_deployment.md
```

导出 ONNX：

```bash
cd /home/wzq/MyProject/realtime_image_service
mkdir -p models

python scripts/export_depth_anything_onnx.py \
  --repo_path ./Depth-Anything-V2-main \
  --checkpoint ./Depth-Anything-V2-main/checkpoints/depth_anything_v2_vits.pth \
  --encoder vits \
  --height 280 \
  --width 280 \
  --device cpu \
  --output ./models/depth_anything_v2_vits_280.onnx
```

启动 ONNX Runtime 后端：

```bash
DEPTH_BACKEND=onnxruntime \
DEPTH_ONNX_PATH=./models/depth_anything_v2_vits_280.onnx \
DEPTH_INPUT_SIZE=280 \
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider \
./scripts/run_depth_server.sh
```

压测 depth_server：

```bash
python scripts/benchmark_depth_server.py \
  --host 127.0.0.1 \
  --port 18080 \
  --width 320 \
  --height 240 \
  --count 50 \
  --warmup 5
```

## Parameters And YAML Config

`image_server` 的检测区域和风险阈值在：

```text
config/image_server.yaml
```

`obstacle_risk_analyzer` 里 ROI 的含义：

```text
roi_x_ratio      ROI 左上角 x，占图像宽度比例
roi_y_ratio      ROI 左上角 y，占图像高度比例
roi_width_ratio  ROI 宽度，占图像宽度比例
roi_height_ratio ROI 高度，占图像高度比例
```

默认配置偏向视角中心偏下区域：

```text
横向：20% 到 90%
纵向：50% 到 95%
```

ROS2 侧参数分三层：

```text
C++ declare_parameter 默认值
  -> YAML 配置文件
  -> launch / shell 环境变量启动覆盖
  -> ros2 param set 运行时动态修改
```

默认 YAML 文件：

```text
ros2_ws/src/image_publisher_pkg/config/network_camera_obstacle_avoidance.yaml
```

不设置环境变量时，启动脚本直接使用 YAML 里的值。启动时可以指定自己的 YAML，或者只覆盖某几个参数：

```bash
./scripts/run_network_camera_pipeline.sh

CONFIG_FILE=/path/to/network_camera_obstacle_avoidance.yaml \
STREAM_URL="http://手机IP:端口/video" \
MAX_REQUEST_FPS=60.0 \
./scripts/run_network_camera_pipeline.sh
```

运行时查看参数：

```bash
ros2 param list /network_camera_node
ros2 param get /network_camera_node fps
ros2 param get /ros2_tcp_client_node max_request_fps
```

运行时动态修改：

```bash
ros2 param set /network_camera_node fps 60.0
ros2 param set /network_camera_node image_width 320
ros2 param set /network_camera_node image_height 240
ros2 param set /network_camera_node stream_url "http://手机IP:端口/video"

ros2 param set /ros2_tcp_client_node max_request_fps 60.0
ros2 param set /safety_controller_node low_speed 0.10
```

如果 `ros2 topic hz /image_service/depth_image` 达不到目标值，先把 `fps` 和 `max_request_fps` 同步降到 30，再视情况降到 15。实时系统优先保证处理最新帧，不追求积压队列里的每一帧都被推理。

当前支持动态生效的参数：

- `network_camera_node`: `stream_url`, `topic`, `fps`, `image_width`, `image_height`, `reconnect_interval`, `frame_id`
- `ros2_tcp_client_node`: `host`, `port`, `topic_name`, `output_path`, `depth_output_path`, `max_request_fps`
- `safety_controller_node`: `low_speed`, `medium_speed`, `stop_on_unknown`

## ROS Topics

| Topic | Type | Description |
| --- | --- | --- |
| `/camera/image_raw` | `sensor_msgs/msg/Image` | 网络摄像头原始图像 |
| `/image_service/processed_image` | `sensor_msgs/msg/Image` | 原图叠加障碍物结果 |
| `/image_service/depth_image` | `sensor_msgs/msg/Image` | 彩色深度图可视化 |
| `/obstacle_result` | `std_msgs/msg/String` | 风险判断 JSON |
| `/image_service/metrics` | `std_msgs/msg/String` | 服务端耗时指标 |
| `/image_service/status` | `std_msgs/msg/String` | 系统状态 |
| `/cmd_vel` | `geometry_msgs/msg/Twist` | 安全速度指令 |

## Resume Summary

可写成：

> 基于 ROS2 + C++17 + Muduo + OpenCV + Depth Anything V2 实现网络摄像头单目实时障碍物风险感知系统。设计 C++ 网络摄像头节点、自定义 TCP 图像协议、Depth Anything V2 推理服务和 ROS2 安全控制链路，采用最新帧缓存、后台限速推理和连接复用降低实时系统延迟，输出实时深度图、风险 JSON、状态指标和安全速度指令。

## Known Limits

- Depth Anything V2 输出相对深度，不能直接作为真实距离。
- 当前目标是感知系统工程原型，不是自动驾驶级避障系统。
- 企业级进一步方向：TensorRT engine、Triton 推理服务、相机标定、rosbag 回放测试、P95/P99 延迟统计。
