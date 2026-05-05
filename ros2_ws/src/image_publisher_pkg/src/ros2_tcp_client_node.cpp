#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp> // 发布字符串消息的头文件

#include "realtime_image_service/tcp_image_client.hpp" // 包含 TCP 客户端类的头文件


class Ros2TcpClientNode     
: public rclcpp::Node
{ 
public:
    Ros2TcpClientNode()
    : Node("ros2_tcp_client_node")
    {
        host_ = declare_parameter<std::string>("host", "127.0.0.1");
        port_ = declare_parameter<uint16_t>("port", 9999);   
        topic_name_ = declare_parameter<std::string>("topic_name", "image_topic");
        output_path_ = declare_parameter<std::string>("output_path", "samples/ros2_result.jpg"); // 前面补上程序启动时的目录

        subscription_ = this->create_subscription<sensor_msgs::msg::Image>(
            topic_name_, 10,
            // 以后收到图像消息时，请调用当前这个对象的 OnImage 函数，并把收到的消息传进去。
            std::bind(&Ros2TcpClientNode::OnImage, this, std::placeholders::_1)
            // [this](const sensor_msgs::msg::Image::SharedPtr msg) { OnImage(msg); }
        );  

        publisher_ = this->create_publisher<sensor_msgs::msg::Image>(
            "image_service/processed_image", 10
        );
    }

private:
    void OnImage(const sensor_msgs::msg::Image::SharedPtr msg)
    {
        try
        {   
            // cv_bridge::toCvCopy(msg, "bgr8")
            //         ↓
            // 返回一个 CvImagePtr
            //         ↓
            // ->image 取出里面的 cv::Mat
            //         ↓
            // 赋值给 const cv::Mat image
            // 将 ROS 图像消息转换为 OpenCV 图像格式，指定编码为 "bgr8"（即 8 位无符号的 BGR 格式）
            const cv::Mat image = cv_bridge::toCvCopy(msg, "bgr8")->image;

            ris::TcpImageClient client(host_, port_);
            std::string error_message; // 用于返回错误信息
            
            // 链接 tcp 服务器
            if (!client.Connect(&error_message))
            {
                RCLCPP_ERROR(this->get_logger(), "%s", error_message.c_str());
                return;
            }

            const uint32_t request_id = ++request_id_; // 生成新的请求 ID
        
            cv::Mat result;
            if (!client.SendImage(image, &result, request_id, &error_message))
            {
                RCLCPP_ERROR(this->get_logger(), "%s", error_message.c_str());
                return;
            }

            std_msgs::msg::Header header; // 创建一个 ROS 消息头
            header.stamp = this->now(); // 设置时间戳为当前时间
            // 如果原始消息的 frame_id 为空，则使用默认值 "camera"，否则使用原始消息的 frame_id
            header.frame_id = msg->header.frame_id.empty() ? "camera" : msg->header.frame_id;
            // 将处理后的图像转换为 ROS 消息格式，并发布到 "result_image" 主题
            auto processed_msg = cv_bridge::CvImage(header, "bgr8", result).toImageMsg();
            publisher_->publish(*processed_msg); /// 发布处理后的图像消息

            if (!cv::imwrite(output_path_, result)) // 保存结果图片
            {
                RCLCPP_ERROR(get_logger(), "failed to write output image: %s", output_path_.c_str());
                return;
            }

            RCLCPP_INFO(get_logger(), "result image saved to: %s", output_path_.c_str());
            RCLCPP_INFO(get_logger(), "request id: %d", request_id);
        } // try
        catch (const cv::Exception& e)
        // std::exception          // 标准异常基类
        // std::runtime_error      // 运行时错误
        // std::logic_error        // 逻辑错误
        // std::out_of_range       // 越界
        // std::invalid_argument   // 参数不合法
        // std::bad_alloc          // 内存分配失败
        {
            RCLCPP_ERROR(get_logger(), "OpenCV error: %s", e.what()); // e.what() 返回错误描述
        }// catch (const std::exception& e)
    }

    std::string host_;
    uint16_t port_;
    std::string topic_name_;
    std::string output_path_;
    uint32_t request_id_ = 0; // 请求 ID，可以根据需要生成唯一 ID
    // 订阅图像消息
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
};

int main(int argc, char const *argv[])
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<Ros2TcpClientNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();

    return 0;
}
