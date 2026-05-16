# Internship Resume Notes

## Project Name

ROS2 实时单目深度感知与可视化系统

## Resume Bullet

- 基于 ROS2 + C++17 + Python 构建实时单目深度感知与可视化系统，实现网络摄像头采集、ROS2 图像发布、Muduo TCP 图像服务、Depth Anything V2 深度推理、深度伪彩色渲染和 ROS2 结果发布。
- 设计自定义 TCP 二进制协议和长连接客户端，支持图像请求、JSON 结果、处理图和深度图打包传输，避免每帧重复连接开销。
- 使用最新帧缓存和 `max_request_fps` 限频策略，将相机采集频率和模型推理频率解耦，减少实时视频链路中的旧帧堆积。
- 支持 PyTorch 与 ONNX Runtime CUDA 两种推理后端，提供 ONNX 导出脚本和 TCP 端到端压测脚本。
- 通过 ROS2 launch/YAML 参数配置相机地址、图像尺寸、服务地址和推理频率，并发布 `/image_service/metrics`、`/image_service/status` 进行性能观测。

## Interview Positioning

这个项目不要描述成“可靠避障系统”。推荐描述为：

> 我做的是一个 ROS2 实时单目深度感知与可视化系统。项目重点是把相机采集、ROS2 topic、C++ TCP 服务、Python 深度模型服务、ONNX Runtime CUDA 推理和深度图实时发布串成完整工程链路。

## Demo Checklist

1. 启动 `depth_server.py`，日志显示 `CUDAExecutionProvider`。
2. 启动 `image_server`，确认监听 `9999`。
3. 启动 ROS2 pipeline。
4. 用 `rqt_image_view` 查看 `/camera/image_raw` 和 `/image_service/depth_image`。
5. 用 `ros2 topic echo /image_service/metrics --once` 展示延迟。
6. 用 `ros2 topic hz /image_service/depth_image` 展示实时刷新频率。

## Key Talking Points

- 为什么拆成 C++ 图像服务和 Python 模型服务。
- TCP 为什么要做包头和长连接。
- 为什么只处理最新帧。
- ONNX Runtime CUDA provider 如何确认。
- Depth Anything V2 是相对深度，适合可视化和近远趋势展示，不等同于真实米制测距。
