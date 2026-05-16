# Interview Q&A

## 1. 这个项目是什么？

这是一个 ROS2 实时单目深度感知与可视化系统。系统用网络摄像头采集图像，通过 ROS2 发布图像话题，C++ 客户端把最新帧发送到 Muduo 图像服务端，服务端调用 Python Depth Anything V2 推理服务生成相对深度图，再渲染成伪彩色深度图并发布回 ROS2。

## 2. 系统链路是什么？

```text
Network camera
-> network_camera_node
-> /camera/image_raw
-> ros2_tcp_client_node
-> C++ image_server
-> Python depth_server.py
-> Depth Anything V2 / ONNX Runtime CUDA
-> /image_service/processed_image
-> /image_service/depth_image
-> /image_service/result
-> /image_service/metrics
-> /image_service/status
```

## 3. 你自己主要做了什么？

我主要实现了 ROS2 C++ 节点、TCP 图像传输协议、Muduo 图像服务端、C++ 到 Python 深度服务的长连接客户端、深度图可视化渲染、YAML/launch 参数配置、启动脚本和性能统计。

## 4. 为什么要把模型放到 Python 服务里？

Depth Anything V2 的官方生态和模型加载主要在 Python/PyTorch 中。把模型封装成独立 Python 服务后，C++ ROS2 和 Muduo 主链路不需要直接依赖 Python 模型代码，也方便后续替换 ONNX Runtime、TensorRT 或其他模型服务。

## 5. 为什么还需要 C++ image_server？

它把图像服务逻辑集中到一个高性能 TCP 服务里，负责协议解析、图像解码、深度服务调用、结果打包和耗时统计。ROS2 节点只负责订阅/发布和调度最新帧，职责更清晰。

## 6. TCP 协议怎么设计？

协议头固定 16 字节：

```text
magic + version + msg_type + payload_size + request_id
```

payload 是 JPEG 图像或响应数据。响应 payload 内部再按长度前缀打包 JSON、处理图和深度图。这样可以处理 TCP 半包、粘包和二进制 JPEG 数据。

## 7. 为什么用长连接？

实时视频每帧都 connect/close 会增加握手、系统调用和资源释放开销。项目中 ROS2 客户端到 image_server、image_server 到 depth_server 都复用连接，失败后再关闭并重连。

## 8. 为什么只处理最新帧？

模型推理可能慢于相机帧率。如果每帧排队，会导致延迟越来越大，最后处理的是旧画面。项目中订阅回调只保存最新帧，后台线程按 `max_request_fps` 采样处理，优先保证实时性。

## 9. 发布了哪些 ROS2 topic？

```text
/camera/image_raw
/image_service/processed_image
/image_service/depth_image
/image_service/result
/image_service/metrics
/image_service/status
```

图像可以用 `rqt_image_view` 或 RViz 查看，JSON 可以用 `ros2 topic echo` 查看。

## 10. 怎么观察性能？

服务端 JSON 中输出：

```text
decode_us
depth_us
encode_us
total_us
```

ROS2 侧 `/image_service/status` 输出当前处理 FPS、最大请求 FPS 和错误状态。也可以用 `ros2 topic hz /image_service/depth_image` 看发布频率。

## 11. Depth Anything V2 输出的是什么？

它输出单目相对深度，不是米制距离。项目中只把它作为相对深度感知和可视化结果展示，不把它解释成真实距离。

## 12. ONNX Runtime CUDA 怎么确认生效？

启动 `depth_server.py` 后查看日志中的 providers，应该包含：

```text
CUDAExecutionProvider
```

如果只看到 `CPUExecutionProvider`，说明当前 Python 环境没有正确加载 GPU 版 ONNX Runtime 或 CUDA/cuDNN 依赖。

## 13. 这个项目适合投什么岗位？

适合 ROS2 开发、机器人软件、机器人视觉、感知系统集成、C++ 工程和模型部署方向的实习岗位。

## 14. 怎么介绍项目边界？

可以说：

> 这个项目重点是 ROS2 实时图像感知链路和深度可视化，不把单目相对深度当作真实米制测距或安全控制。后续如果接真实机器人，可以融合 RGB-D、双目或激光雷达。
