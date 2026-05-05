# 基于电脑本地摄像头的 ROS2-Muduo 单目视觉辅助避障系统方案

## 0. 方案调整说明

本方案不使用 GEC6818。  
系统直接使用电脑本地 USB 摄像头或笔记本摄像头作为图像输入，运行环境集中在一台 Ubuntu/ROS2 电脑上。

调整后的定位是：

> 基于本地摄像头、ROS2、Muduo、OpenCV 与单目深度估计的视觉辅助避障原型系统。

该方案适合用于机器人视觉感知、ROS2 工程项目、C++ 网络服务和边缘视觉服务框架展示。

需要注意：

> 单目深度估计不是严格米制深度，不能替代激光雷达、RGB-D 深度相机或安全雷达。  
> 本项目应定位为“视觉辅助避障 / 风险提示原型”，不应宣传为高可靠安全避障系统。

---

## 1. 项目目标

在原有 Realtime Image Service 项目基础上，将输入从本地图像文件扩展为电脑本地摄像头实时图像流，并增加单目深度估计和前方障碍物风险判断。

原始项目链路：

```text
ROS2 image_file_publisher
    ↓
sensor_msgs/msg/Image
    ↓
ros2_tcp_client_node
    ↓
JPEG over TCP
    ↓
Muduo image_server
    ↓
CUDA / CPU Sobel
    ↓
返回处理图像
```

升级后链路：

```text
本地 USB 摄像头 / 笔记本摄像头
    ↓
ROS2 camera_node
    ↓ /camera/image_raw
ros2_tcp_client_node
    ↓ JPEG over TCP
Muduo image_server
    ↓
Depth Estimation Module
    ↓
ROI Risk Analyzer
    ↓
返回处理图像 + JSON 结果
    ↓
ROS2 发布 /obstacle_result
    ↓
safety_controller_node
    ↓ /cmd_vel
```

---

## 2. 系统整体架构

```text
┌──────────────────────────────┐
│        Local Camera           │
│  USB Camera / Laptop Camera   │
└───────────────┬──────────────┘
                │
                v
┌──────────────────────────────┐
│        ROS2 camera_node       │
│ publish /camera/image_raw     │
└───────────────┬──────────────┘
                │ sensor_msgs/msg/Image
                v
┌──────────────────────────────┐
│      ros2_tcp_client_node     │
│ cv_bridge -> cv::Mat          │
│ JPEG encode                   │
│ TCP request                   │
└───────────────┬──────────────┘
                │ JPEG over TCP
                v
┌──────────────────────────────┐
│       Muduo image_server      │
│ protocol parse                │
│ OpenCV decode                 │
│ depth estimation              │
│ ROI risk analysis             │
│ result encode                 │
└───────────────┬──────────────┘
                │ processed image + JSON
                v
┌──────────────────────────────┐
│      ros2_tcp_client_node     │
│ publish result/status/metrics │
└───────────────┬──────────────┘
                │
                v
┌──────────────────────────────┐
│   safety_controller_node      │
│ subscribe /obstacle_result    │
│ publish /cmd_vel              │
└──────────────────────────────┘
```

---

## 3. ROS2 负责什么

ROS2 在本项目里主要负责系统集成，不直接负责图像算法计算。

ROS2 的职责：

```text
1. 接入本地摄像头图像
2. 统一图像消息格式
3. 将图像交给 TCP 客户端节点
4. 接收图像服务处理结果
5. 发布障碍物风险结果
6. 发布系统状态和性能指标
7. 给下游控制节点提供输入
8. 用 RViz / rqt / rosbag2 做调试和记录
```

---

## 4. 核心 ROS2 Topic 设计

### 4.1 输入图像

```text
/camera/image_raw
sensor_msgs/msg/Image
```

说明：

```text
电脑本地摄像头发布的原始 RGB 图像。
```

---

### 4.2 处理后图像

```text
/image_service/processed_image
sensor_msgs/msg/Image
```

说明：

```text
服务端返回的可视化图像，例如深度热力图、ROI 框、风险文字。
```

---

### 4.3 障碍物风险结果

```text
/obstacle_result
std_msgs/msg/String
```

示例：

```json
{
  "obstacle": true,
  "region": "front_center",
  "risk_level": "medium",
  "suggest_action": "slow_down",
  "near_ratio": 0.24,
  "confidence": 0.78
}
```

---

### 4.4 图像服务性能指标

```text
/image_service/metrics
std_msgs/msg/String
```

示例：

```json
{
  "backend": "depth_anything_v2",
  "decode_us": 1450,
  "depth_us": 38200,
  "risk_us": 310,
  "encode_us": 940,
  "total_us": 41850
}
```

---

### 4.5 图像服务状态

```text
/image_service/status
std_msgs/msg/String
```

示例：

```json
{
  "server_connected": true,
  "camera_active": true,
  "fps": 12.6,
  "error_code": 0
}
```

---

### 4.6 控制输出

```text
/cmd_vel
geometry_msgs/msg/Twist
```

说明：

```text
safety_controller_node 根据 /obstacle_result 发布速度控制指令。
```

---

## 5. 单目深度估计模块

### 5.1 推荐模型

第一版推荐使用：

```text
Depth Anything V2
```

原因：

```text
1. 单目图像深度估计效果较好
2. Python/PyTorch 版本容易跑通
3. 不需要 RGB-D 深度相机
4. 适合做机器人视觉辅助避障原型
```

---

### 5.2 部署方式

第一版不建议直接把 Depth Anything V2 强行塞进 C++。

推荐方式：

```text
Muduo C++ image_server
    ↓
调用 Python depth_server
    ↓
Python 跑 Depth Anything V2
    ↓
返回 depth map 或风险结果
```

这样可以减少 C++ 集成 PyTorch 的复杂度。

后续升级方向：

```text
1. Python depth_server
2. ONNX Runtime
3. TensorRT
4. C++ 推理服务
```

---

## 6. ROI 风险判断算法

### 6.1 前方危险区域 ROI

对于移动机器人，图像中间偏下区域通常对应机器人正前方近距离区域。

ROI 可以初步设置为：

```cpp
int roi_x = width * 0.25;
int roi_y = height * 0.45;
int roi_w = width * 0.50;
int roi_h = height * 0.45;
```

示意：

```text
┌──────────────────────────────┐
│                              │
│          far region           │
│                              │
│        ┌──────────┐          │
│        │ front ROI │          │
│        │ risk area │          │
│        └──────────┘          │
└──────────────────────────────┘
```

---

### 6.2 深度归一化

单目深度模型输出通常是相对深度。

需要统一为：

```text
depth_norm 越小 = 越近
depth_norm 越大 = 越远
```

归一化流程：

```text
raw_depth
    ↓
去除异常值
    ↓
min-max normalize
    ↓
必要时反转深度方向
    ↓
得到 depth_norm
```

---

### 6.3 near_ratio 计算

在 ROI 中统计近处像素比例：

```text
near_ratio = 近处像素数量 / ROI 总像素数量
```

示例阈值：

```text
depth_norm < 0.35 认为是近处像素
```

---

### 6.4 风险等级判断

第一版可以使用简单规则：

```text
near_ratio > 0.35        high      stop
near_ratio > 0.18        medium    slow_down
否则                     low       keep
```

伪代码：

```cpp
if (near_ratio > 0.35) {
    obstacle = true;
    risk_level = "high";
    suggest_action = "stop";
} else if (near_ratio > 0.18) {
    obstacle = true;
    risk_level = "medium";
    suggest_action = "slow_down";
} else {
    obstacle = false;
    risk_level = "low";
    suggest_action = "keep";
}
```

---

## 7. Muduo 服务端修改方案

### 7.1 原服务端流程

```text
receive TCP request
    ↓
parse fixed header
    ↓
decode JPEG
    ↓
CPU / CUDA Sobel
    ↓
encode JPEG
    ↓
send response
```

---

### 7.2 升级后流程

```text
receive TCP request
    ↓
parse fixed header
    ↓
decode JPEG
    ↓
depth estimation
    ↓
depth normalize
    ↓
ROI risk analysis
    ↓
draw ROI / risk text
    ↓
encode processed image
    ↓
send processed image + JSON result
```

---

### 7.3 新增模块

建议新增：

```text
include/
  depth_estimator.hpp
  obstacle_risk_analyzer.hpp
  result_json.hpp

src/
  depth_estimator.cpp
  obstacle_risk_analyzer.cpp
  result_json.cpp
```

---

### 7.4 ObstacleResult 数据结构

```cpp
struct ObstacleResult {
    bool obstacle{false};
    std::string region{"front_center"};
    std::string risk_level{"low"};
    std::string suggest_action{"keep"};
    float near_ratio{0.0f};
    float confidence{0.0f};
    int roi_x{0};
    int roi_y{0};
    int roi_w{0};
    int roi_h{0};
};
```

---

## 8. 协议升级建议

原协议：

```text
[magic:4][version:2][msg_type:2][payload_size:4][request_id:4][payload]
```

原协议可以继续保留。

为了返回 JSON + 图片，可以设计两种方式。

---

### 8.1 简单版：只返回处理图，JSON 写日志

适合第一版快速跑通。

优点：

```text
改动小
保持原协议
容易调试
```

缺点：

```text
ROS2 侧拿不到结构化结果
```

---

### 8.2 推荐版：返回 JSON + 图片

payload 内部结构：

```text
[json_size:4][json_bytes][image_size:4][image_bytes]
```

完整响应：

```text
[fixed_header][json_size][json_bytes][image_size][image_bytes]
```

这样客户端可以同时拿到：

```text
1. 处理后图像
2. 风险结果 JSON
3. 性能指标 JSON
```

---

## 9. ROS2 客户端修改方案

`ros2_tcp_client_node` 需要完成：

```text
1. 订阅 /camera/image_raw
2. 使用 cv_bridge 转 cv::Mat
3. JPEG 编码
4. 按固定头协议发送给 Muduo 服务端
5. 接收服务端响应
6. 解析 JSON + 图片
7. 发布 /obstacle_result
8. 发布 /image_service/processed_image
9. 发布 /image_service/metrics
10. 发布 /image_service/status
```

---

## 10. safety_controller_node 设计

### 10.1 输入

```text
/obstacle_result
std_msgs/msg/String
```

---

### 10.2 输出

```text
/cmd_vel
geometry_msgs/msg/Twist
```

---

### 10.3 控制逻辑

```text
risk_level = high
    linear.x = 0.0
    angular.z = 0.0

risk_level = medium
    linear.x = 0.05
    angular.z = 0.0

risk_level = low
    linear.x = 0.2
    angular.z = 0.0
```

---

### 10.4 注意

第一版可以只做控制逻辑验证，不一定要接真实机器人。

可以在终端观察：

```bash
ros2 topic echo /cmd_vel
```

---

## 11. 本地摄像头接入方式

### 11.1 使用 usb_cam 包

安装：

```bash
sudo apt update
sudo apt install ros-humble-usb-cam
```

运行：

```bash
ros2 run usb_cam usb_cam_node_exe
```

查看 topic：

```bash
ros2 topic list
```

确认存在：

```text
/image_raw
```

可以通过 remap 改成：

```bash
ros2 run usb_cam usb_cam_node_exe --ros-args -r /image_raw:=/camera/image_raw
```

---

### 11.2 使用 OpenCV 自写 camera_node

如果不用 `usb_cam`，也可以自写 ROS2 camera node：

```text
cv::VideoCapture cap(0)
    ↓
read frame
    ↓
cv_bridge
    ↓
publish sensor_msgs/msg/Image
```

这种方式更适合你自己掌控代码。

---

## 12. 开发路线

### V1：本地摄像头 ROS2 图像流

目标：

```text
电脑摄像头发布 /camera/image_raw
```

验证：

```bash
ros2 topic list
ros2 topic echo /camera/image_raw --once
```

---

### V2：复用原 TCP 图像服务链路

目标：

```text
/camera/image_raw
    ↓
ros2_tcp_client_node
    ↓
Muduo image_server
```

服务端先继续返回 Sobel 图，确认实时链路没问题。

---

### V3：接入 Python 深度估计

目标：

```text
Muduo 服务端收到图像后，调用 Python depth_server 得到 depth map。
```

第一版也可以先离线：

```text
test.jpg -> depth.png
```

确认模型效果后再服务化。

---

### V4：ROI 风险判断

目标：

```text
depth map
    ↓
ROI
    ↓
near_ratio
    ↓
risk_level
    ↓
suggest_action
```

输出：

```json
{
  "obstacle": true,
  "risk_level": "medium",
  "suggest_action": "slow_down",
  "near_ratio": 0.24
}
```

---

### V5：ROS2 发布 /obstacle_result

目标：

```text
ros2_tcp_client_node 解析服务端结果，并发布 /obstacle_result。
```

验证：

```bash
ros2 topic echo /obstacle_result
```

---

### V6：安全控制节点

目标：

```text
safety_controller_node 订阅 /obstacle_result 并发布 /cmd_vel。
```

验证：

```bash
ros2 topic echo /cmd_vel
```

---

### V7：演示和文档

需要补充：

```text
README.md
docs/design.md
docs/protocol.md
docs/benchmark.md
docs/limitations.md
demo video
```

---

## 13. Benchmark 设计

建议测试以下指标：

```text
1. 摄像头输入分辨率
2. JPEG 编码耗时
3. 网络传输耗时
4. Muduo 解码耗时
5. 深度估计耗时
6. ROI 风险分析耗时
7. 响应编码耗时
8. 端到端总耗时
9. 输出 FPS
```

建议表格：

```text
Resolution | Depth Backend | Encode(ms) | Inference(ms) | Risk(ms) | Total(ms) | FPS
640x480    | PyTorch       |             |               |          |           |
320x240    | PyTorch       |             |               |          |           |
640x480    | ONNX          |             |               |          |           |
```

---

## 14. 项目限制

必须在文档中说明：

```text
1. 单目深度估计不是严格米制深度。
2. 该模块不能作为唯一安全避障来源。
3. 光照、反光、透明物体、纯色墙面可能导致深度估计不稳定。
4. ROI 规则是启发式方法，需要针对摄像头安装角度调参。
5. 工业级机器人应融合激光雷达、RGB-D 相机、安全雷达、碰撞条等传感器。
6. 本项目定位为视觉辅助避障原型，不是安全认证避障系统。
```

---

## 15. 简历写法

### 15.1 简洁版

> 基于 ROS2、Muduo、OpenCV 与单目深度估计构建移动机器人视觉辅助避障原型，使用本地摄像头发布 ROS2 图像流，客户端通过自定义 TCP 协议将图像发送至 Muduo 服务端，服务端完成深度估计、ROI 风险分析和结果回传，ROS2 侧发布 `/obstacle_result` 与 `/cmd_vel`，实现前方障碍物风险判断和减速/停车控制闭环。

---

### 15.2 工程版

> 在原有 ROS2-Muduo-CUDA 实时图像服务基础上，扩展单目视觉辅助避障模块。系统采用 ROS2 接入本地 USB 摄像头图像流，使用 `cv_bridge` 转换图像并通过自定义 TCP 固定头协议发送至 Muduo 服务端；服务端完成协议解析、OpenCV 解码、单目深度估计、ROI 近障碍物比例计算和风险等级判断，并返回可视化图像与 JSON 结果。ROS2 侧将结果发布为 `/obstacle_result`、`/image_service/metrics` 和 `/image_service/processed_image`，同时通过安全控制节点输出 `/cmd_vel`，形成视觉辅助减速/停车闭环。

---

## 16. 最终定位

本项目最终应定位为：

> 面向移动机器人场景的单目视觉辅助避障原型系统。

它的核心价值不是“完全替代激光雷达避障”，而是展示：

```text
1. ROS2 图像系统集成能力
2. C++ Muduo 网络服务能力
3. 自定义协议设计能力
4. OpenCV 图像编解码能力
5. 单目深度估计接入能力
6. 机器人风险判断业务闭环
7. 性能统计与工程化分析能力
```
