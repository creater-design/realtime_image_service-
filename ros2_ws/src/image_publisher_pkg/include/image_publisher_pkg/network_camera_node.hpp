#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace image_publisher_pkg
{

class NetworkCameraNode : public rclcpp::Node
{
public:
    NetworkCameraNode();
    ~NetworkCameraNode() override;

private:
    bool openStream();
    void closeStream();
    void onTimer();
    void tryReconnect();
    void resetTimer();
    rcl_interfaces::msg::SetParametersResult onSetParameters(
        const std::vector<rclcpp::Parameter>& parameters);

    std::string stream_url_;
    std::string topic_;
    std::string frame_id_;
    double fps_{60.0};
    int image_width_{320};
    int image_height_{240};
    double reconnect_interval_{2.0};

    cv::VideoCapture cap_;
    cv::Mat frame_;
    std::mutex mutex_;

    std::chrono::steady_clock::time_point last_reconnect_attempt_{
        std::chrono::steady_clock::time_point::min()
    };

    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr parameter_callback_handle_;
};

}  // namespace image_publisher_pkg
