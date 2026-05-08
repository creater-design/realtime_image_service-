# 推理部署：PyTorch -> ONNX Runtime -> TensorRT

## 目标

当前项目的实时瓶颈主要在 Depth Anything V2 推理。推理部署的目标是把模型推理从“研究代码可运行”推进到“工程部署可观测、可替换、可压测”。

当前支持两种后端，项目默认使用 `onnxruntime`，需要回退调试时再显式设置 `DEPTH_BACKEND=pytorch`：

```text
pytorch       调试后端，直接加载 Depth Anything V2 checkpoint
onnxruntime   默认部署后端，加载导出的 ONNX 模型
```

后续 TensorRT 推荐路线：

```text
PyTorch checkpoint -> ONNX -> ONNX Runtime CUDAExecutionProvider -> TensorRTExecutionProvider / TensorRT engine
```

## 文件

```text
scripts/depth_server.py                  TCP 推理服务，支持 pytorch/onnxruntime 后端
scripts/run_depth_server.sh              统一启动入口
scripts/export_depth_anything_onnx.py    PyTorch checkpoint 导出 ONNX
scripts/benchmark_depth_server.py        TCP 推理服务压测脚本
models/                                  本地模型目录，ONNX/engine 不提交 Git
```

## 依赖

PyTorch 后端：

```bash
pip install torch torchvision opencv-python numpy
```

ONNX 导出：

```bash
pip install onnx
```

ONNX Runtime GPU：

```bash
pip install onnxruntime-gpu
```

注意：当前项目默认只走 GPU，安装 `onnxruntime-gpu`，不要用 CPU 版 `onnxruntime` 作为运行后端。

## 1. 先验证 PyTorch 后端

```bash
cd /home/wzq/MyProject/realtime_image_service

DEPTH_BACKEND=pytorch \
DEPTH_INPUT_SIZE=280 \
DEPTH_DEVICE=cuda \
DEPTH_PRECISION=fp16 \
./scripts/run_depth_server.sh
```

另开终端压测：

```bash
cd /home/wzq/MyProject/realtime_image_service

python scripts/benchmark_depth_server.py \
  --host 127.0.0.1 \
  --port 18080 \
  --width 320 \
  --height 240 \
  --count 30 \
  --warmup 3
```

记录输出中的：

```text
avg_ms
p95_ms
fps
```

## 2. 导出 ONNX

推荐先用 `vits + 280x280`，这是更适合实时演示的轻量配置。

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

说明：

- `height` 和 `width` 必须能被 14 整除，因为 Depth Anything V2 的 ViT patch size 是 14。
- ONNX 模型默认固定输入尺寸，利于后续 TensorRT 优化。
- 导出的 `.onnx` 文件被 `.gitignore` 忽略，不要提交到 GitHub。

如果机器上 CUDA/PyTorch 环境正常，也可以用 GPU 导出：

```bash
python scripts/export_depth_anything_onnx.py \
  --repo_path ./Depth-Anything-V2-main \
  --checkpoint ./Depth-Anything-V2-main/checkpoints/depth_anything_v2_vits.pth \
  --encoder vits \
  --height 280 \
  --width 280 \
  --device cuda \
  --output ./models/depth_anything_v2_vits_280.onnx
```

## 3. 启动 ONNX Runtime GPU 后端

```bash
cd /home/wzq/MyProject/realtime_image_service

DEPTH_BACKEND=onnxruntime \
DEPTH_ONNX_PATH=./models/depth_anything_v2_vits_280.onnx \
DEPTH_INPUT_SIZE=280 \
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider \
./scripts/run_depth_server.sh
```

项目默认只走 GPU，不使用 CPU fallback。如果 ONNX Runtime CUDA 不可用，服务会直接启动失败并打印当前可用 providers。常见原因：

```text
没有安装 onnxruntime-gpu
CUDA/cuDNN 版本不匹配
当前 Python 环境不是你安装 onnxruntime-gpu 的环境
```

## 4. 对比压测

先启动某个后端，然后运行：

```bash
python scripts/benchmark_depth_server.py \
  --host 127.0.0.1 \
  --port 18080 \
  --width 320 \
  --height 240 \
  --count 50 \
  --warmup 5 \
  --output /tmp/depth_server_benchmark_depth.jpg
```

建议记录成表：

| Backend | input_size | Provider | avg_ms | p95_ms | FPS |
| --- | ---: | --- | ---: | ---: | ---: |
| PyTorch | 280 | CUDA fp16 | 待测 | 待测 | 待测 |
| ONNX Runtime | 280 | CUDA | 8.31 | 9.77 | 120.36 |

上表中的 ONNX Runtime CUDA 数值来自当前机器 `320x240` 请求、`count=50`、`warmup=5` 的 TCP 端到端压测。实际速度会受 GPU、CUDA/cuDNN、摄像头流、网络和 ROS2 发布链路影响。

## 5. 接入完整 ROS2 管线

ONNX Runtime depth_server 启动后，`image_server` 和 ROS2 管线不用改，因为 TCP 协议保持不变。

终端 1：

```bash
DEPTH_BACKEND=onnxruntime \
DEPTH_ONNX_PATH=./models/depth_anything_v2_vits_280.onnx \
DEPTH_INPUT_SIZE=280 \
DEPTH_ORT_PROVIDERS=CUDAExecutionProvider \
./scripts/run_depth_server.sh
```

终端 2：

```bash
./scripts/run_image_server.sh
```

终端 3：

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

60 FPS 目标需要满足两个条件：

```text
1. 手机视频流本身能提供接近 60 FPS 的帧率。
2. depth_server 使用 ONNX Runtime CUDA 后端，并且 ros2 topic hz /image_service/depth_image 接近目标值。
```

## 6. TensorRT 下一步

当前项目已经完成 TensorRT 前置条件：

```text
1. 模型可以导出 ONNX
2. 推理服务支持 ONNX Runtime 后端
3. 后端切换不影响 C++ image_server 和 ROS2 节点
4. 有 benchmark 脚本可以量化延迟
```

后续可以选两条路线：

```text
路线 A：ONNX Runtime TensorRTExecutionProvider
DEPTH_ORT_PROVIDERS=TensorrtExecutionProvider,CUDAExecutionProvider

路线 B：trtexec 生成 .engine，再单独实现 TensorRT Runtime 后端
```

找实习项目中，当前做到 ONNX Runtime 后端已经足够说明你有推理部署意识。TensorRT 可以作为下一步优化方向继续补。
