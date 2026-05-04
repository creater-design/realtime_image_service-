#include <memory>
#include <string>
#include <chrono>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "cv_bridge/cv_bridge.h"
#include <opencv2/opencv.hpp>

using namespace std::chrono_literals;

class ImageFilePublisher
: public rclcpp::Node
{
public:
    ImageFilePublisher()
    : Node("image_file_publisher")
    {
        // 从参数服务器获取图片路径、主题名称和发布间隔
        // 它比普通赋值多了一个能力：运行时可以从外部改参数值，而不需要重新编译代码
        image_path_ =  declare_parameter<std::string>("image_path", "sample/test.jpg");
        topic_name_ = declare_parameter<std::string>("topic_name", "image_topic");
        publish_interval_ms_ = declare_parameter<int>("publish_interval_ms", 1000);

        publisher_ = this->create_publisher<sensor_msgs::msg::Image>(topic_name_, 10);
        // 为什么不用while？
        // 这种写法不适合 ROS2，因为会阻塞节点执行，影响订阅、服务、回调等其他功能。
        // 参数1：发布间隔，单位为毫秒；参数2：回调函数，这里绑定了成员函数 OnTimer
        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(publish_interval_ms_),
            // 参数1：成员函数指针，参数2：对象指针；std::bind 可以绑定成员函数和对象，使其成为一个可调用对象
            std::bind(&ImageFilePublisher::OnTimer, this)
        );
    }

private:
    // 定时器回调函数，用于读取图片并发布
    void OnTimer()
    {
        // // 用 OpenCV 从本地路径读取图片，IMREAD_COLOR 表示按彩色图读取
        cv::Mat image = cv::imread(image_path_, cv::IMREAD_COLOR);
        if (image.empty()) 
        {
            // 打印错误日志并返回
            RCLCPP_ERROR(this->get_logger(), "Failed to read image file");
            return;
        }

        // 将 OpenCV 图像转换为 ROS2 消息
        // 参数1：ROS消息头，包含时间戳 stamp 和坐标系 frame_id；这里是默认空Header
        // 参数2：图像编码格式，表示8位、3通道、BGR顺序；OpenCV彩色图默认就是BGR
        // 参数3：OpenCV图像数据
        auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), "bgr8", image).toImageMsg();

        // 发布消息
        publisher_->publish(*msg);

        // 打印日志
        RCLCPP_INFO(get_logger(), "published image to %s", topic_name_.c_str());
    }

    // 保存图片的文件路径
    std::string image_path_;
    // 发布的主题名称
    std::string topic_name_;
    // 发布间隔，单位为毫秒
    int publish_interval_ms_ = 1000; 
    // ROS2 图片发布器对象
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
    // ROS2 定时器对象
    rclcpp::TimerBase::SharedPtr timer_;
    
};

int main(int argc, char const *argv[])
{
    rclcpp::init(argc, argv); // 初始化 ROS2，必须在使用任何 ROS2 功能之前调用
    rclcpp::spin(std::make_shared<ImageFilePublisher>()); // 创建 ImageFilePublisher 节点实例，并进入循环等待回调函数执行
    rclcpp::shutdown(); // 关闭 ROS2，释放资源，通常在程序结束前调用
    
    return 0;
}
