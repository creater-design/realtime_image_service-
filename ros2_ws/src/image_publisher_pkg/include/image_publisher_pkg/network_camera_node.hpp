#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp> // 主要用于 ROS2 参数动态修改回调的返回结果
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace image_publisher_pkg
{

// NetworkCameraNode：网络摄像头图像发布节点
// 主要功能：
// 1. 通过 OpenCV VideoCapture 打开网络视频流
// 2. 定时读取图像帧
// 3. 将图像封装成 ROS2 sensor_msgs::msg::Image 消息并发布
// 4. 支持断线重连
// 5. 支持运行时动态修改部分参数
class NetworkCameraNode 
: public rclcpp::Node
{
public:
    // 构造函数：初始化节点、参数、发布器、定时器等
    NetworkCameraNode();

    // 析构函数：释放摄像头资源
    ~NetworkCameraNode() override; // override 是C++11 新特性，主要用在子类重写父类虚函数

private:
    // 打开网络视频流
    bool openStream();

    // 关闭当前视频流
    void closeStream();

    // 定时器回调函数
    // 按照设定 fps 周期性读取图像并发布
    void onTimer();

    // 尝试重新连接网络视频流
    void tryReconnect();

    // 根据当前 fps 重置定时器周期
    void resetTimer();

    // 参数动态更新回调函数
    // 当用户通过 ros2 param set 修改参数时会触发
    rcl_interfaces::msg::SetParametersResult onSetParameters(
        const std::vector<rclcpp::Parameter>& parameters);

private:
    // 网络视频流地址
    std::string stream_url_;

    // ROS2 图像发布话题名
    std::string topic_;

    // 图像消息中的坐标系名称
    std::string frame_id_;

    // 图像发布帧率
    double fps_ {60.0};

    // 输出图像宽度
    int image_width_ {320};

    // 输出图像高度
    int image_height_ {240};

    // 断线重连间隔，单位：秒
    double reconnect_interval_ {2.0};

    // OpenCV 视频流对象
    cv::VideoCapture cap_;

    // 当前读取到的图像帧
    cv::Mat frame_;

    // 互斥锁，用于保护视频流和图像帧相关资源
    std::mutex mutex_;

    // 上一次尝试重连的时间
    std::chrono::steady_clock::time_point last_reconnect_attempt_ {
        std::chrono::steady_clock::time_point::min() // 初始化为最小值，这样做的好处是：程序刚启动时，第一次判断重连通常会直接通过。
    };

    // ROS2 定时器
    rclcpp::TimerBase::SharedPtr timer_;

    // ROS2 图像发布器
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;

    // ROS2 参数动态更新回调句柄
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr parameter_callback_handle_;
};

}  // namespace image_publisher_pkg