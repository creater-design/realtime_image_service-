# 简历项目写法：ROS2 单目实时深度估计与障碍物风险感知系统

## 项目名称

ROS2 单目实时深度估计与障碍物风险感知系统

可选简短名称：

```text
ROS2 Real-Time Monocular Obstacle Risk Perception System
```

## 推荐技术栈

```text
C++17 / ROS2 Humble / Muduo / OpenCV / Depth Anything V2 / PyTorch / Python / YAML / Linux / WSL2
```

如果简历空间有限，写成：

```text
C++17、ROS2、Muduo、OpenCV、Depth Anything V2、PyTorch
```

## 一句话项目介绍

基于 ROS2、C++17、Muduo、OpenCV 和 Depth Anything V2 实现实时单目障碍物风险感知系统，支持手机网络摄像头图像采集、深度估计、ROI 风险分析、结果图发布和安全速度控制。

## 简历项目描述

```text
基于 ROS2 Humble、C++17、Muduo、OpenCV 和 Depth Anything V2 实现网络摄像头单目实时障碍物风险感知系统。系统通过 C++ ROS2 节点接入手机 HTTP MJPEG/RTSP 视频流，发布实时图像话题；ROS2 客户端将最新帧通过自定义 TCP 协议发送到 Muduo 图像服务端；服务端调用 Python Depth Anything V2 推理服务生成深度图，并基于可配置 ROI 和深度阈值输出障碍物风险 JSON、深度可视化图和安全速度指令。
```

## 简历 Bullet 版本

推荐写 4 到 6 条，不要全部堆满。

```text
- 基于 ROS2 Humble、C++17、Muduo、OpenCV 和 Depth Anything V2 构建实时单目障碍物风险感知系统，完成网络摄像头采集、深度估计、风险判断和安全控制闭环。
- 实现 C++ network_camera_node，使用 OpenCV VideoCapture 接入手机 HTTP MJPEG/RTSP 视频流，发布 /camera/image_raw，替代 WSL2 下不稳定的 USB 摄像头透传方案。
- 设计自定义 TCP 图像推理协议，基于 Muduo 实现 C++ 图像服务端，完成图像请求解析、JPEG 编解码、JSON 结果封装和异常响应处理。
- 将原有逐帧短连接调用重构为长连接复用，结合最新帧缓存、后台限速推理和旧帧丢弃策略，降低实时链路延迟并避免 ROS 图像回调阻塞。
- 接入 Depth Anything V2 作为独立 Python 推理服务，C++ 服务端通过 TCP 调用模型服务，实现模型推理环境与 ROS2/C++ 实时链路解耦。
- 实现基于深度图下半区域 ROI 的障碍物风险分析算法，支持 high/medium/low 风险分级、confidence 计算、深度图可视化和 /cmd_vel 安全速度输出。
- 将图像流地址、推理频率、服务端地址、ROI 区域和风险阈值配置化到 YAML/ROS2 参数中，支持不同场景快速调参和部署。
- 增加 metrics/status 观测话题，输出端到端处理耗时、服务状态和处理 FPS，便于调试实时性能瓶颈。
```

## 最推荐简历写法

如果你的简历只能放一个项目，建议这样写：

```text
ROS2 单目实时深度估计与障碍物风险感知系统 | C++17, ROS2, Muduo, OpenCV, Depth Anything V2

- 基于 ROS2 Humble、C++17、Muduo 和 OpenCV 构建实时单目障碍物风险感知系统，实现手机网络摄像头图像采集、深度估计、风险判断和安全速度控制链路。
- 实现 C++ network_camera_node 接入 HTTP MJPEG/RTSP 视频流并发布 /camera/image_raw；实现 ros2_tcp_client_node 将最新图像帧通过自定义 TCP 协议发送至 Muduo 图像服务端。
- 基于 Muduo 实现 C++ 图像推理服务，设计 JPEG 图像请求、结果 JSON、深度图和结果图的二进制响应格式，支持连接复用、异常处理和服务状态观测。
- 接入 Depth Anything V2 推理服务生成单目相对深度图，支持 PyTorch/ONNX Runtime 后端切换，并实现基于下半区域 ROI 的障碍物风险分级算法，输出 /obstacle_result、/image_service/depth_image 和 /cmd_vel。
- 针对实时性瓶颈，采用最新帧缓存、后台限频推理、旧帧丢弃、长连接复用和 YAML 参数化配置，降低图像回调阻塞并提升系统可调试性。
```

## 面试时怎么介绍

更完整的面试高频问题和回答见：

```text
docs/interview_qa.md
```

### 1. 项目为什么这么设计？

```text
我把系统拆成 ROS2 C++ 实时链路、Muduo C++ 图像服务和 Python 深度模型服务三部分。ROS2 负责机器人侧话题通信和控制输出，Muduo 负责高性能 TCP 图像服务，Python 服务负责 Depth Anything V2 推理。这样可以把模型环境和实时通信解耦，后续替换 ONNX/TensorRT 或其他模型也比较方便。
```

### 2. 为什么不用 USB 摄像头？

```text
项目最开始尝试过 WSL2 USB 摄像头透传，但 usbipd 在 Windows 重启、睡眠唤醒或 wsl --shutdown 后容易导致 /dev/video0 丢失。为了让项目更稳定，我改成手机 IP Webcam/RTSP 网络摄像头，C++ ROS2 节点直接通过 OpenCV VideoCapture 读取网络视频流。
```

### 3. 为什么用 Muduo，不自己写网络库？

```text
Muduo 是成熟的 C++ Reactor 网络库，企业项目通常不会重复实现这类基础设施。我在项目中的重点是基于 Muduo 实现业务层图像推理服务，包括自定义协议、长连接复用、图像编解码、结果封装和异常处理。
```

### 4. 实时性怎么优化？

```text
早期 PyTorch/CPU 链路推理速度低于摄像头帧率，如果每帧同步推理会导致 ROS 回调堆积。项目采用最新帧缓存和后台推理线程，始终处理最新图像；同时用 max_request_fps 限制推理频率，用长连接复用减少 TCP connect/close 开销。最终部署侧切到 ONNX Runtime CUDA，320x240 请求压测 avg 8.31ms、P95 9.77ms、约 120 FPS，项目默认目标配置为 60 FPS。
```

### 5. 风险判断怎么做？

```text
Depth Anything V2 输出的是相对深度，不是米制距离。所以我没有直接说距离多少米，而是在图像下半区域 ROI 内统计近距离区域比例、有效深度比例和连通区域面积，根据阈值输出 low/medium/high 风险等级。ROI 和阈值都放在 YAML 中，方便针对不同摄像头角度和场景调参。
```

## 项目亮点

```text
1. C++ 主链路完整：ROS2 节点、TCP 客户端、Muduo 服务端、风险分析都以 C++ 实现。
2. 系统拆分合理：ROS2 通信、C++ 网络服务、Python 模型推理解耦。
3. 有实时性意识：ONNX Runtime CUDA、最新帧缓存、限频、长连接、旧帧丢弃、分辨率控制。
4. 有工程化配置：YAML 配置 ROI、阈值、服务地址、推理频率。
5. 有可观测性：输出结果图、深度图、风险 JSON、metrics、status 和 cmd_vel。
6. 能讲清楚边界：Depth Anything V2 是相对深度，项目定位是风险感知原型，不是精确测距系统。
```

## 不建议这样写

不要写：

```text
自研 Muduo 网络库
自研 Depth Anything V2 深度模型
实现自动驾驶级避障系统
实现厘米级单目测距
```

推荐写：

```text
基于 Muduo 实现 C++ 图像推理服务
接入 Depth Anything V2 实现单目相对深度估计
实现障碍物风险感知原型系统
基于 ROI 和相对深度统计进行风险分级
```

## 简历投递方向

这个项目适合投：

```text
C++ 开发实习
机器人软件实习
ROS 开发实习
计算机视觉工程实习
自动驾驶感知/工具链实习
边缘 AI 部署实习
```

如果投 C++ 岗，重点强调：

```text
Muduo、TCP 协议、长连接、异常处理、C++ 模块拆分、系统性能优化
```

如果投机器人/ROS 岗，重点强调：

```text
ROS2 节点、topic 通信、launch、参数配置、cmd_vel 控制输出、实时图像链路
```

如果投视觉/AI 岗，重点强调：

```text
Depth Anything V2、深度图、ROI 风险分析、OpenCV 图像处理、ONNX/TensorRT 后续优化方向
```

## GitHub README 简短介绍

可以放到 GitHub 仓库简介里：

```text
A ROS2 + C++ real-time monocular obstacle risk perception system using network camera streams, Muduo TCP image service, OpenCV, and Depth Anything V2.
```

中文版本：

```text
基于 ROS2、C++、Muduo、OpenCV 和 Depth Anything V2 的网络摄像头单目实时障碍物风险感知系统。
```

## 后续继续完善的方向

```text
1. 增加 Dockerfile 固化 ROS2、Muduo、OpenCV、Python 模型环境。
2. 增加 rosbag 回放和自动化性能测试。
3. 增加 P50/P95/P99 延迟统计报告。
4. 继续补 TensorRT engine 后端或 Triton 推理服务部署。
5. 增加单元测试和 GitHub Actions 编译检查。
```
