#include "image_publisher_pkg/network_camera_node.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <limits>

#include <cv_bridge/cv_bridge.h>  // 将 OpenCV 的 cv::Mat 转换成 ROS2 的 sensor_msgs::msg::Image
#include <opencv2/imgproc.hpp>
#include <std_msgs/msg/header.hpp>

namespace image_publisher_pkg
{

// 构造函数：创建 network_camera_node 节点，并初始化参数、发布器、视频流和定时器
NetworkCameraNode::NetworkCameraNode()
: Node("network_camera_node")
{
    // 声明并读取网络摄像头地址
    stream_url_ = this->declare_parameter<std::string>("stream_url", "http://192.168.31.89:8080/video");

    // 声明并读取图像发布话题
    topic_ = this->declare_parameter<std::string>("topic", "/camera/image_raw");

    // 声明并读取发布帧率
    fps_ = this->declare_parameter<double>("fps", 60.0);

    // 声明并读取图像宽度
    image_width_ = this->declare_parameter<int>("image_width", 320);

    // 声明并读取图像高度
    image_height_ = this->declare_parameter<int>("image_height", 240);

    // 声明并读取断线重连间隔
    reconnect_interval_ = this->declare_parameter<double>("reconnect_interval", 2.0);

    // 声明并读取图像坐标系名称
    // 表示该图像是从哪个坐标系/传感器坐标系采集到的
    frame_id_ = declare_parameter<std::string>("frame_id", "camera_link");

    // 检查 fps 参数是否合法
    if (fps_ <= 0.0)
    {
        RCLCPP_WARN(get_logger(), "fps must be > 0, reset to 60.0");
        fps_ = 60.0;
    }

    // 检查重连间隔是否合法
    if (reconnect_interval_ <= 0.0)
    {
        RCLCPP_WARN(get_logger(), "reconnect_interval must be > 0, reset to 2.0");
        reconnect_interval_ = 2.0;
    }

    // 打印当前节点参数，方便启动时确认配置是否正确
    RCLCPP_INFO(
        get_logger(),
        // 相邻的字符串字面量会在编译期自动拼接
        "network_camera_node parameters: stream_url=%s topic=%s fps=%.2f "
        "image_width=%d image_height=%d reconnect_interval=%.2f frame_id=%s",
        stream_url_.c_str(),
        topic_.c_str(),
        fps_,
        image_width_,
        image_height_,
        reconnect_interval_,
        frame_id_.c_str()
    );

    // 创建 ROS2 图像发布器
    // SensorDataQoS 适合图像、雷达、IMU 这类高频传感器数据
    image_pub_ = this->create_publisher<sensor_msgs::msg::Image>(topic_, rclcpp::SensorDataQoS());

    // 启动时先尝试打开一次网络摄像头
    // 如果 URL 写错，可以马上在终端日志中看到错误
    openStream();

    // 根据 fps 创建定时器
    resetTimer();

    // 注册参数动态修改回调
    // 后续 ros2 param set 修改参数时，会自动调用 onSetParameters()
    parameter_callback_handle_ = this->add_on_set_parameters_callback(
        std::bind(&NetworkCameraNode::onSetParameters, this, std::placeholders::_1)
    );
}

// 析构函数：节点退出时关闭视频流
NetworkCameraNode::~NetworkCameraNode()
{
    closeStream(); 
}

// 打开网络摄像头视频流
bool NetworkCameraNode::openStream()
{
    // 先关闭旧的视频流，避免重复打开
    closeStream();

    // 如果视频流地址为空，直接返回失败
    if (stream_url_.empty())
    {
        RCLCPP_ERROR(get_logger(), "stream_url is empty");
        return false;
    }

    RCLCPP_INFO(get_logger(), "opening network camera stream: %s", stream_url_.c_str());

    // 使用 OpenCV VideoCapture 打开网络视频流
    if (!cap_.open(stream_url_))
    {
        RCLCPP_ERROR_THROTTLE(
            get_logger(),
            *get_clock(), // 获取当前时间
            5000,         // 5 秒内重复打印一次错误日志
            "failed to open network camera stream: %s",
            stream_url_.c_str()
        );

        return false;
    }

    // 设置较小缓冲区，只是缓冲一帧，尽量发布最新帧
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);

    // 尝试设置摄像头帧率
    cap_.set(cv::CAP_PROP_FPS, fps_);

    // 如果 image_width_ > 0，就尝试设置视频流宽度
    if (image_width_ > 0)
    {
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, image_width_);
    }

    // 如果 image_height_ > 0，就尝试设置视频流高度
    if (image_height_ > 0)
    {
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, image_height_);
    }

    RCLCPP_INFO(get_logger(), "network camera stream opened");

    return true;
}

// 关闭网络摄像头视频流
void NetworkCameraNode::closeStream()
{
    if (cap_.isOpened())
    {
        cap_.release();
    }
}

// 根据当前 fps 重新创建定时器
void NetworkCameraNode::resetTimer()
{
    // 定时器周期 = 1 / fps
    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / fps_)
    );

    // 创建周期性定时器，到时间后调用 onTimer()
    timer_ = this->create_wall_timer(period, std::bind(&NetworkCameraNode::onTimer, this));
}

// 尝试重新连接网络摄像头
void NetworkCameraNode::tryReconnect()
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - last_reconnect_attempt_).count(); // 获取当前时间间隔

    // 如果距离上次重连时间还没有超过 reconnect_interval_，则不重连
    // 这样可以避免频繁重连导致日志刷屏或 CPU 忙等
    if (last_reconnect_attempt_ != std::chrono::steady_clock::time_point::min() &&
        elapsed < reconnect_interval_)
    {
        return;
    }

    // 记录本次重连尝试时间
    last_reconnect_attempt_ = now;

    RCLCPP_WARN(get_logger(), "trying to reconnect network camera stream");

    // 尝试重新打开视频流
    openStream();
}

// 定时器回调函数：周期性读取图像并发布到 ROS2 话题
void NetworkCameraNode::onTimer()
{
    // 加锁保护 cap_ 和 frame_
    std::lock_guard<std::mutex> lock(mutex_);

    // 如果视频流未打开，则尝试重连
    if (!cap_.isOpened())
    {
        tryReconnect();
        return;
    }

    // 从网络摄像头读取一帧图像
    if (!cap_.read(frame_) || frame_.empty())
    {
        RCLCPP_WARN_THROTTLE(
            get_logger(),
            *get_clock(),
            2000,
            "failed to read frame from network camera stream"
        );

        // 读取失败后关闭视频流，并尝试重连, 重连不上表示摄像头已断线
        closeStream();
        tryReconnect();
        return;
    }

    // 默认直接发布原始帧
    cv::Mat publish_frame = frame_;

    // 如果当前帧尺寸和目标尺寸不同，则进行 resize
    if (image_width_ > 0 && image_height_ > 0 && (frame_.cols != image_width_ || frame_.rows != image_height_))
    {
        cv::resize(frame_, publish_frame, cv::Size(image_width_, image_height_));
    }

    // 创建 ROS2 消息头，ROS 的 header 只有一下两部分
    // header.stamp     // 时间戳
    // header.frame_id  // 坐标系名字
    std_msgs::msg::Header header;

    // 设置时间戳为当前 ROS 时间
    header.stamp = now();

    // 设置图像坐标系名称
    header.frame_id = frame_id_;

    // 使用 cv_bridge 将 OpenCV 的 cv::Mat 转换成 ROS2 Image 消息
    auto msg = cv_bridge::CvImage(header, "bgr8", publish_frame).toImageMsg();

    // 发布图像消息
    image_pub_->publish(*msg);
}

// 参数动态修改回调函数
// 当使用 ros2 param set 修改参数时，会进入这个函数
rcl_interfaces::msg::SetParametersResult NetworkCameraNode::onSetParameters(const std::vector<rclcpp::Parameter>& parameters)
{
    // 创建参数设置结果
    rcl_interfaces::msg::SetParametersResult result;

    // 默认认为参数修改成功
    result.successful = true;

    // 第一轮循环：先检查参数是否合法
    for (const auto& parameter : parameters)
    {
        const std::string& name = parameter.get_name();

        // 检查字符串参数
        if (name == "stream_url" || name == "topic" || name == "frame_id")
        {
            if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_STRING || parameter.as_string().empty())
            {
                result.successful = false;
                result.reason = name + " must be a non-empty string";

                return result;
            }
        }
        // 检查 double 类型参数
        else if (name == "fps" || name == "reconnect_interval")
        {
            if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE || parameter.as_double() <= 0.0)
            {
                result.successful = false;
                result.reason = name + " must be a positive double";

                return result;
            }
        }
        // 检查整数类型参数
        else if (name == "image_width" || name == "image_height")
        {
            if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_INTEGER || parameter.as_int() < 0)
            {
                result.successful = false;
                result.reason = name + " must be an integer >= 0";

                return result;
            }
        }
    }

    // 参数合法后再加锁修改成员变量
    std::lock_guard<std::mutex> lock(mutex_);

    // 是否需要重新打开视频流
    bool reopen_stream = false;

    // 是否需要重新创建发布器
    bool recreate_publisher = false;

    // 是否需要重新创建定时器
    bool recreate_timer = false;

    // 第二轮循环：真正应用参数修改
    for (const auto& parameter : parameters)
    {
        const std::string& name = parameter.get_name();

        if (name == "stream_url")
        {
            stream_url_ = parameter.as_string();
            reopen_stream = true;
        }
        else if (name == "topic")
        {
            topic_ = parameter.as_string();
            recreate_publisher = true;
        }
        else if (name == "fps")
        {
            fps_ = parameter.as_double();
            recreate_timer = true;
        }
        else if (name == "image_width")
        {
            const int64_t value = parameter.as_int();
            if (value < 0 || value > std::numeric_limits<int>::max())
            {
                result.successful = false;
                result.reason = name + "image_width must be a positive int value";

                return result;
            }

            image_width_ = static_cast<int>(parameter.as_int());
        }
        else if (name == "image_height")
        {
            const int64_t value = parameter.as_int();
            if (value < 0 || value > std::numeric_limits<int>::max())
            {
                result.successful = false;
                result.reason = name + "image_height must be a positive int value";

                return result;
            }

            image_height_ = static_cast<int>(parameter.as_int());
        }
        else if (name == "reconnect_interval")
        {
            reconnect_interval_ = parameter.as_double();
        }
        else if (name == "frame_id")
        {
            frame_id_ = parameter.as_string();
        }
    }

    // 如果 topic 被修改，需要重新创建发布器
    if (recreate_publisher)
    {
        image_pub_ = this->create_publisher<sensor_msgs::msg::Image>(topic_, rclcpp::SensorDataQoS());
    }

    // 如果 fps 被修改，需要重新创建定时器
    if (recreate_timer)
    {
        resetTimer();
    }

    // 如果 stream_url 被修改，需要重新打开视频流
    if (reopen_stream)
    {
        // 重置重连时间，确保修改 URL 后可以马上尝试连接
        last_reconnect_attempt_ = std::chrono::steady_clock::time_point::min();

        openStream();
    }

    // 打印更新后的参数
    RCLCPP_INFO(
        get_logger(),
        "updated parameters: stream_url=%s topic=%s fps=%.2f image_width=%d "
        "image_height=%d reconnect_interval=%.2f frame_id=%s",
        stream_url_.c_str(),
        topic_.c_str(),
        fps_,
        image_width_,
        image_height_,
        reconnect_interval_,
        frame_id_.c_str()
    );

    return result;
}

}  // namespace image_publisher_pkg

// 程序入口函数
int main(int argc, char** argv)
{
    // 初始化 ROS2
    rclcpp::init(argc, argv);

    // 创建 NetworkCameraNode 节点，并进入循环
    rclcpp::spin(std::make_shared<image_publisher_pkg::NetworkCameraNode>());

    // 关闭 ROS2
    rclcpp::shutdown();

    return 0;
}