# Resume Project Writeup

## Short Version

基于 ROS2、C++17、Muduo、OpenCV、Python 和 Depth Anything V2 实现实时单目深度感知与可视化系统，支持网络摄像头采集、C++ TCP 图像服务、ONNX Runtime CUDA 深度推理、实时深度图渲染、ROS2 topic 发布和端到端延迟统计。

## Long Version

本项目实现了一个 ROS2 实时单目深度感知与可视化系统。系统通过 C++ ROS2 节点接入手机 IP Webcam、HTTP MJPEG 或 RTSP 视频流，发布 `/camera/image_raw`；ROS2 TCP 客户端缓存最新帧，并按设定频率发送到 C++ Muduo 图像服务端；服务端调用独立 Python Depth Anything V2 推理服务生成相对深度图，再渲染为伪彩色深度图并返回给 ROS2 节点；最终发布处理图、深度图、结果 JSON、耗时 metrics 和运行状态。

## Architecture

```text
Network camera
  -> ROS2 network_camera_node
  -> /camera/image_raw
  -> ROS2 ros2_tcp_client_node
  -> C++ TcpImageClient
  -> Muduo image_server
  -> Python depth_server.py
  -> Depth Anything V2 / ONNX Runtime CUDA
  -> depth colormap rendering
  -> /image_service/depth_image
  -> /image_service/result
  -> /image_service/metrics
  -> /image_service/status
```

## My Work

- 实现 `network_camera_node`，使用 OpenCV 读取网络摄像头并发布 ROS2 图像话题。
- 实现 `ros2_tcp_client_node`，支持最新帧缓存、后台工作线程、推理限频、结果图和深度图发布。
- 设计 C++ TCP 客户端和自定义协议，处理包头、payload 长度、request id、半包/粘包和错误响应。
- 使用 Muduo 实现 C++ `image_server`，完成图像解码、深度服务调用、深度伪彩色渲染、JSON 结果和图像打包。
- 封装 Python `depth_server.py`，支持 PyTorch 和 ONNX Runtime CUDA 后端，提供 warmup、provider 检查和持久 TCP 连接。
- 提供 YAML/launch/脚本配置，支持相机地址、图像尺寸、推理频率、服务端地址和调试图片路径配置。
- 增加 FPS、decode/depth/encode/total 延迟统计，并通过 ROS2 topic 输出。

## Resume Bullet

> 基于 ROS2 + C++17 + Python 构建实时单目深度感知与可视化系统，实现网络摄像头图像采集、Muduo TCP 图像服务、Depth Anything V2 深度推理、ONNX Runtime CUDA 加速、深度伪彩色渲染和 ROS2 topic 发布；设计自定义二进制协议和长连接客户端，使用最新帧缓存与推理限频降低实时链路延迟，并通过 `/image_service/metrics` 和 `/image_service/status` 输出 FPS 与端到端耗时。

## Boundaries

Depth Anything V2 输出的是相对深度，不是米制距离。本项目主目标是实时深度感知、可视化和工程链路集成，不把输出描述成真实距离测量或安全控制。
