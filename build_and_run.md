# Build And Run Guide

本文档对应当前项目定位：ROS2 实时单目深度感知与可视化系统。

运行链路：

```text
Network camera
  -> ROS2 /camera/image_raw
  -> C++ image_server
  -> Python depth_server.py
  -> Depth Anything V2 / ONNX Runtime CUDA
  -> /image_service/processed_image
  -> /image_service/depth_image
  -> /image_service/result
  -> /image_service/metrics
  -> /image_service/status
```

## 1. Environment

项目目录：

```bash
cd /home/wzq/MyProject/realtime_image_service
```

需要准备：

```text
ROS2 Humble
OpenCV
Muduo
yaml-cpp
Conda Python environment for Depth Anything V2
ONNX Runtime GPU
Depth Anything V2 repo and checkpoint
```

确认 GPU 可见：

```bash
nvidia-smi
```

确认 ONNX Runtime GPU provider：

```bash
/home/wzq/miniconda3/envs/NAVGS/bin/python - <<'PY'
import onnxruntime as ort
print(ort.__version__)
print(ort.get_available_providers())
PY
```

正常应该能看到：

```text
CUDAExecutionProvider
```

## 2. Prepare Model

第三方 Depth Anything V2 仓库放在：

```text
Depth-Anything-V2-main/
```

权重文件放在：

```text
Depth-Anything-V2-main/checkpoints/depth_anything_v2_vits.pth
```

ONNX 模型放在：

```text
models/depth_anything_v2_vits_280.onnx
models/depth_anything_v2_vits_280.onnx.data
```

如果需要重新导出 ONNX：

```bash
mkdir -p models

/home/wzq/miniconda3/envs/NAVGS/bin/python scripts/export_depth_anything_onnx.py \
  --repo_path ./Depth-Anything-V2-main \
  --checkpoint ./Depth-Anything-V2-main/checkpoints/depth_anything_v2_vits.pth \
  --encoder vits \
  --height 280 \
  --width 280 \
  --device cpu \
  --output ./models/depth_anything_v2_vits_280.onnx
```

## 3. Build C++ Image Server

```bash
cd /home/wzq/MyProject/realtime_image_service

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

生成文件：

```text
build/image_server
```

如果 Muduo 路径不一致，可以显式指定：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMUDUO_ROOT=/usr/local/include \
  -DMUDUO_BUILD_ROOT=/usr/local
```

## 4. Build ROS2 Package

ROS2 编译建议不要在 Conda 环境里执行。

```bash
cd /home/wzq/MyProject/realtime_image_service/ros2_ws

conda deactivate
source /opt/ros/humble/setup.bash

colcon build --packages-select image_publisher_pkg --symlink-install \
  --cmake-args \
  -Dfmt_DIR=/usr/lib/x86_64-linux-gnu/cmake/fmt \
  -DPython3_EXECUTABLE=/usr/bin/python3

source install/setup.bash
```

如果之前构建过旧版本，建议清理包级构建产物后重编：

```bash
cd /home/wzq/MyProject/realtime_image_service
rm -rf ros2_ws/build/image_publisher_pkg ros2_ws/install/image_publisher_pkg ros2_ws/log

cd ros2_ws
source /opt/ros/humble/setup.bash
colcon build --packages-select image_publisher_pkg --symlink-install \
  --cmake-args \
  -Dfmt_DIR=/usr/lib/x86_64-linux-gnu/cmake/fmt \
  -DPython3_EXECUTABLE=/usr/bin/python3
```

## 5. Run

需要三个终端。

### Terminal 1: Depth Server

```bash
cd /home/wzq/MyProject/realtime_image_service
conda activate NAVGS

PYTHON_BIN=/home/wzq/miniconda3/envs/NAVGS/bin/python \
DEPTH_BACKEND=onnxruntime \
DEPTH_ONNX_PATH=./models/depth_anything_v2_vits_280.onnx \
DEPTH_INPUT_SIZE=280 \
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider \
DEPTH_RESPONSE_CODEC=jpg \
DEPTH_JPEG_QUALITY=90 \
./scripts/run_depth_server.sh
```

看到类似日志表示正常：

```text
providers=['CUDAExecutionProvider', 'CPUExecutionProvider']
listening on 127.0.0.1:18080
```

### Terminal 2: C++ Image Server

```bash
cd /home/wzq/MyProject/realtime_image_service
./scripts/run_image_server.sh
```

默认配置文件：

```text
config/image_server.yaml
```

默认监听：

```text
127.0.0.1:9999
```

如果只想验证 ROS2/TCP 链路，不启动真实深度模型：

```bash
NO_DEPTH_SERVER=1 ./scripts/run_image_server.sh
```

### Terminal 3: ROS2 Camera Pipeline

先准备网络摄像头地址，例如手机 IP Webcam：

```text
http://手机IP:端口/video
```

启动 ROS2 管线：

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

该脚本会启动：

```text
network_camera_node
ros2_tcp_client_node
```

不会启动风险预测或 cmd_vel 控制节点。

## 6. View Results

打开新终端：

```bash
conda deactivate
source /opt/ros/humble/setup.bash
source /home/wzq/MyProject/realtime_image_service/ros2_ws/install/setup.bash
```

查看图像频率：

```bash
ros2 topic hz /camera/image_raw
ros2 topic hz /image_service/depth_image
```

查看图像：

```bash
ros2 run rqt_image_view rqt_image_view
```

选择以下 topic：

```text
/camera/image_raw
/image_service/processed_image
/image_service/depth_image
```

查看 JSON 结果：

```bash
ros2 topic echo /image_service/result --once
ros2 topic echo /image_service/metrics --once
ros2 topic echo /image_service/status --once
```

## 7. Useful Parameters

ROS2 pipeline 常用环境变量：

```bash
STREAM_URL="http://手机IP:端口/video"
IMAGE_WIDTH=320
IMAGE_HEIGHT=240
FPS=60.0
MAX_REQUEST_FPS=60.0
OUTPUT_PATH=""
DEPTH_OUTPUT_PATH=""
```

Depth server 常用环境变量：

```bash
DEPTH_BACKEND=onnxruntime
DEPTH_INPUT_SIZE=280
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider
DEPTH_RESPONSE_CODEC=jpg
DEPTH_JPEG_QUALITY=90
```

如果要降低延迟：

```text
降低 IMAGE_WIDTH / IMAGE_HEIGHT
降低 DEPTH_INPUT_SIZE
降低 MAX_REQUEST_FPS
关闭 OUTPUT_PATH / DEPTH_OUTPUT_PATH 写盘
```

## 8. Quick Test Without Camera

测试 depth_server：

```bash
/home/wzq/miniconda3/envs/NAVGS/bin/python scripts/benchmark_depth_server.py \
  --host 127.0.0.1 \
  --port 18080 \
  --width 320 \
  --height 240 \
  --count 50 \
  --warmup 5
```

测试 image_server 能否启动：

```bash
timeout 3s ./build/image_server --config config/image_server.yaml --no-depth-server
```

## 9. Common Issues

### ONNX Runtime 看不到 CUDAExecutionProvider

检查：

```bash
/home/wzq/miniconda3/envs/NAVGS/bin/python -m pip show onnxruntime onnxruntime-gpu
```

如果同时装了 CPU 版和 GPU 版，可能 CPU wheel 覆盖了 GPU runtime。可修复为：

```bash
/home/wzq/miniconda3/envs/NAVGS/bin/python -m pip uninstall -y onnxruntime
/home/wzq/miniconda3/envs/NAVGS/bin/python -m pip install --force-reinstall --no-deps onnxruntime-gpu==1.23.2
```

### ROS2 命令被 Conda 干扰

运行 ROS2 前退出 Conda：

```bash
conda deactivate
source /opt/ros/humble/setup.bash
source /home/wzq/MyProject/realtime_image_service/ros2_ws/install/setup.bash
```

### rqt_image_view 看不到图

先确认 topic 是否存在：

```bash
ros2 topic list
```

再确认频率：

```bash
ros2 topic hz /camera/image_raw
ros2 topic hz /image_service/depth_image
```

### 网络摄像头打不开

先单独测试：

```bash
python scripts/test_network_camera_stream.py --url "http://手机IP:端口/video"
```

如果 RTSP 不稳定，优先使用 HTTP MJPEG。

## 10. Expected Demo

面试或展示时建议按这个顺序：

1. 展示三个终端都正常运行。
2. `rqt_image_view` 展示原图和深度图实时刷新。
3. `ros2 topic echo /image_service/metrics --once` 展示耗时。
4. `ros2 topic hz /image_service/depth_image` 展示发布频率。
5. 说明 Depth Anything V2 输出相对深度，项目重点是 ROS2 实时感知链路和可视化工程。
