# 实习投递项目讲法

## 项目名称

ROS2 单目实时深度估计与障碍物风险感知系统

## 一句话介绍

用 USB 摄像头采集实时图像，通过 ROS2 发布图像流，C++ Muduo 服务负责高性能图像通信和风险分析，Python Depth Anything V2 服务负责单目深度估计，最终输出深度图、障碍物风险 JSON 和安全速度指令。

## 简历 bullet

- 基于 ROS2 Humble、C++17、Muduo、OpenCV 和 Depth Anything V2 实现单目摄像头实时障碍物风险感知系统，支持实时结果图、深度图和风险 JSON 发布。
- 设计自定义 TCP 图像协议，完成 JPEG 图像请求、JSON 结果和多图像 payload 的封装与解析，实现 C++ 服务端和 ROS2 客户端通信。
- 将深度估计模型独立为 Python 推理服务，C++ 服务端通过 TCP 调用，降低模型环境与实时通信模块的耦合。
- 针对 Depth Anything V2 推理耗时高的问题，ROS 客户端采用最新帧缓存、后台限速推理、连接复用和旧帧丢弃策略，避免阻塞摄像头回调。
- 实现 ROI 深度统计和风险等级判断，输出 `low/medium/high` 风险、建议动作和 confidence，并通过 ROS2 发布 `/cmd_vel` 安全控制指令。
- 添加 metrics/status 话题和调试图片输出，支持端到端延迟、处理 FPS、服务连接状态的运行观测。

## 面试展开讲法

### 为什么要拆成 C++ 服务和 Python 模型服务？

Depth Anything V2 依赖 PyTorch，部署和 CUDA 环境更适合 Python；ROS2 图像接收、TCP 通信和控制链路更适合 C++。拆开后，模型可以单独升级，C++ 实时链路不被模型环境污染。

### 为什么不是每帧都推理？

深度模型单帧耗时远高于摄像头帧间隔。如果每帧同步推理，ROS 回调会堆积，看到的是过期画面。项目采用最新帧缓存和后台限速推理，牺牲部分帧率换取低延迟和系统稳定性。

### 单目深度有什么问题？

Depth Anything V2 输出相对深度，不是米制距离。它适合做近远趋势和风险区域判断，但不能直接替代双目、RGB-D 或激光雷达。项目里用 ROI 内的相对深度分布做风险判断，并保留了这个边界说明。

### 企业级下一步怎么做？

- TensorRT/ONNX 加速推理。
- image_server 到 depth_server 使用持久连接或进程内推理。
- 加相机标定和真实距离标注数据。
- 做 rosbag 回放测试和自动化性能报告。
- 把阈值、ROI、速度策略放到 YAML 配置。

## 演示 checklist

1. 打开三个终端启动 depth_server、image_server、ROS pipeline。
2. `rqt_image_view` 展示 `/image_service/processed_image`。
3. `rqt_image_view` 展示 `/image_service/depth_image`。
4. `ros2 topic echo /obstacle_result --once` 展示风险 JSON。
5. `ros2 topic hz /image_service/depth_image` 展示实时 FPS。
6. 解释 `MAX_REQUEST_FPS` 和 `DEPTH_INPUT_SIZE` 怎么影响实时性。
