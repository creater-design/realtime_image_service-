# ROS2 Real-Time Monocular Depth Visualization

基于 ROS2 Humble、C++17、Muduo TCP、OpenCV、Python 和 Depth Anything V2 的实时单目深度感知与可视化系统。

主链路：

```text
Network camera / RTSP / HTTP MJPEG
  -> ROS2 network_camera_node publishes /camera/image_raw
  -> ROS2 ros2_tcp_client_node sends latest frames to C++ image_server
  -> C++ Muduo image_server calls Python Depth Anything V2 service
  -> ONNX Runtime CUDA / PyTorch depth inference
  -> C++ depth colormap rendering
  -> ROS2 processed image, depth image, result JSON, metrics, status
  -> RViz / rqt_image_view / OpenCV display
```

项目重点是工程链路：ROS2 图像采集、最新帧缓存、TCP 长连接、模型服务解耦、深度图实时渲染、YAML/launch 参数化和 FPS/延迟统计。

## Layout

```text
include/realtime_image_service/        C++ 公共头文件
config/                                image_server YAML 配置
src/common/                            协议、图像编解码、深度服务客户端、payload/json
src/server/                            Muduo image_server
ros2_ws/src/image_publisher_pkg/       ROS2 C++ 节点、launch、YAML
scripts/                               启动脚本、ONNX 导出、depth_server
models/                                本地 ONNX/TensorRT 模型产物，不提交大文件
docs/                                  部署、性能和项目说明
```

`Depth-Anything-V2-main/` 是第三方 Depth Anything V2 官方模型仓库，只用于加载 PyTorch 模型和导出 ONNX。系统运行默认使用 `models/depth_anything_v2_vits_280.onnx`。

## Features

- `network_camera_node` 使用 `cv::VideoCapture(stream_url)` 读取 HTTP MJPEG 或 RTSP 流。
- `ros2_tcp_client_node` 保存最新帧并按 `max_request_fps` 限速请求，避免模型推理阻塞相机回调。
- `image_server` 使用 Muduo 处理 TCP 图像请求，并复用到 Python depth_server 的长连接。
- `depth_server.py` 支持 PyTorch 和 ONNX Runtime CUDA 后端。
- 服务端返回原图、深度伪彩色图和 JSON 指标。
- ROS2 发布 `/camera/image_raw`、`/image_service/processed_image`、`/image_service/depth_image`、`/image_service/result`、`/image_service/metrics`、`/image_service/status`。

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
conda deactivate  # 如果当前终端在 conda 环境中，先退出
source /opt/ros/humble/setup.bash
colcon build --packages-select image_publisher_pkg --symlink-install \
  --cmake-args \
  -Dfmt_DIR=/usr/lib/x86_64-linux-gnu/cmake/fmt \
  -DPython3_EXECUTABLE=/usr/bin/python3
source install/setup.bash
```

## Run

终端 1：启动 Python 深度推理服务。

```bash
cd /home/wzq/MyProject/realtime_image_service

DEPTH_BACKEND=onnxruntime \
DEPTH_ONNX_PATH=./models/depth_anything_v2_vits_280.onnx \
DEPTH_INPUT_SIZE=280 \
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider \
DEPTH_RESPONSE_CODEC=jpg \
./scripts/run_depth_server.sh
```

终端 2：启动 Muduo 图像服务。

```bash
cd /home/wzq/MyProject/realtime_image_service
./scripts/run_image_server.sh
```

默认读取：

```text
config/image_server.yaml
```

终端 3：启动 ROS2 网络摄像头管线。

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

## View

查看图像话题：

```bash
ros2 run rqt_image_view rqt_image_view
```

可选择：

```text
/camera/image_raw
/image_service/processed_image
/image_service/depth_image
```

查看服务结果和性能：

```bash
ros2 topic echo /image_service/result --once
ros2 topic echo /image_service/metrics --once
ros2 topic echo /image_service/status --once
ros2 topic hz /image_service/depth_image
```

## Topics

| Topic | Type | Description |
| --- | --- | --- |
| `/camera/image_raw` | `sensor_msgs/msg/Image` | 网络摄像头输入图像 |
| `/image_service/processed_image` | `sensor_msgs/msg/Image` | 服务端返回的原图/处理图 |
| `/image_service/depth_image` | `sensor_msgs/msg/Image` | Depth Anything V2 深度伪彩色图 |
| `/image_service/result` | `std_msgs/msg/String` | 本次处理结果 JSON |
| `/image_service/metrics` | `std_msgs/msg/String` | 服务端耗时 JSON |
| `/image_service/status` | `std_msgs/msg/String` | ROS2 客户端状态、FPS、错误信息 |

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

- `MAX_REQUEST_FPS` 控制深度推理请求频率，不等于摄像头发布频率。
- 卡顿优先降低手机 App 输出分辨率，再调整 ROS 侧尺寸。
- `DEPTH_RESPONSE_CODEC=jpg` 优先保证实时性；需要无损深度图时改成 `png`。
- 默认不写调试图片到磁盘，避免 60 FPS 下 JPEG 写盘影响实时性。
- Depth Anything V2 输出相对深度，不是米制距离。

## ONNX Export

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
