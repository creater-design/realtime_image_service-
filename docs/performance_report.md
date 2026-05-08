# 实时性优化记录

## 当前目标

```text
网络摄像头发布：320x240 / 60 FPS
ROS2 TCP 请求：最多 60 FPS，只处理最新帧
Depth Anything V2：ONNX Runtime CUDAExecutionProvider
调试图片落盘：默认关闭
```

这个目标不是保证任意环境都固定 60 FPS，而是把项目默认配置调整到 60 FPS 级实时链路，并提供可验证的压测和观测方法。

## 已完成优化

1. 推理后端默认切换到 ONNX Runtime。
2. `depth_server.py` 默认只使用 `CUDAExecutionProvider`，GPU 不可用时直接报错，不走 CPU fallback。
3. `run_depth_server.sh` 自动把 Conda 环境的 `lib` 加入 `LD_LIBRARY_PATH`，减少 CUDA/cuDNN 动态库加载问题。
4. `network_camera_node` 默认发布 320x240 / 60 FPS，并使用 `SensorDataQoS`。
5. `ros2_tcp_client_node` 使用最新帧缓存和后台线程，只处理最新帧，避免旧帧排队。
6. `ros2_tcp_client_node` 默认 `max_request_fps=60.0`。
7. 结果图和深度图默认不写磁盘，避免 60 FPS 下 JPEG 落盘拖慢。
8. `image_server` 复用到 `depth_server` 的持久 TCP 连接，避免每帧 connect/close。
9. 服务端请求日志降频，避免高帧率下日志成为瓶颈。

## 当前压测结果

压测命令：

```bash
python scripts/benchmark_depth_server.py \
  --host 127.0.0.1 \
  --port 18080 \
  --width 320 \
  --height 240 \
  --count 50 \
  --warmup 5
```

结果：

| Backend | input_size | Provider | avg_ms | p95_ms | max_ms | FPS |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| ONNX Runtime | 280 | CUDAExecutionProvider | 8.31 | 9.77 | 10.14 | 120.36 |

结论：

```text
模型服务本身已经具备 60 FPS 余量。
完整 ROS2 管线能否达到 60 FPS，主要看手机视频流实际帧率、OpenCV 解码、ROS2 发布、JPEG 编解码和显示工具开销。
```

## 运行时检查

```bash
ros2 topic hz /camera/image_raw
ros2 topic hz /image_service/depth_image
ros2 topic echo /image_service/status --once
```

判断方式：

```text
/camera/image_raw 接近 60：网络摄像头输入达标
/image_service/depth_image 接近 60：完整推理链路达标
status.fps 接近 60：ROS2 TCP 客户端处理达标
```

如果达不到 60，优先按顺序检查：

```text
1. depth_server 日志中 providers 是否包含 CUDAExecutionProvider。
2. 手机 IP Webcam 是否真的输出 60 FPS。
3. 是否打开了 rqt_image_view 等高开销显示工具。
4. 是否设置了 OUTPUT_PATH / DEPTH_OUTPUT_PATH 导致逐帧写盘。
5. 是否使用了过高分辨率或 RTSP 高延迟流。
```

## 推荐启动配置

```bash
DEPTH_BACKEND=onnxruntime \
DEPTH_ONNX_PATH=./models/depth_anything_v2_vits_280.onnx \
DEPTH_INPUT_SIZE=280 \
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider \
./scripts/run_depth_server.sh
```

```bash
STREAM_URL="http://手机IP:端口/video" \
IMAGE_WIDTH=320 \
IMAGE_HEIGHT=240 \
FPS=60.0 \
MAX_REQUEST_FPS=60.0 \
OUTPUT_PATH="" \
DEPTH_OUTPUT_PATH="" \
./scripts/run_network_camera_pipeline.sh
```

## 降级策略

如果完整链路达不到 60 FPS，不要让请求堆积。先把摄像头发布和推理请求同步降到 30：

```bash
ros2 param set /network_camera_node fps 30.0
ros2 param set /ros2_tcp_client_node max_request_fps 30.0
```

仍然不稳定再降到 15：

```bash
ros2 param set /network_camera_node fps 15.0
ros2 param set /ros2_tcp_client_node max_request_fps 15.0
```
