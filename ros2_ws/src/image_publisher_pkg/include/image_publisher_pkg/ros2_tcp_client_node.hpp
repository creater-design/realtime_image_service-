#pragma once

#include <rclcpp/rclcpp.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/string.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "realtime_image_service/tcp_image_client.hpp"

namespace realtime_image_service
{

class Ros2TcpClientNode : public rclcpp::Node
{
public:
    Ros2TcpClientNode();
    ~Ros2TcpClientNode() override;

private:
    struct ServerProcessingResult
    {
        cv::Mat processed_image;
        cv::Mat depth_image;
        std::string result_json;
    };

    struct FrameSnapshot
    {
        cv::Mat image;
        std_msgs::msg::Header header;
        uint64_t sequence{0};
    };

    void OnImage(const sensor_msgs::msg::Image::SharedPtr msg);
    void ProcessLoop();
    bool WaitForNextFrame(std::chrono::steady_clock::time_point next_allowed_time,
                          FrameSnapshot* snapshot);
    void ProcessFrame(const cv::Mat& image, const std_msgs::msg::Header& header);
    bool ProcessFrameWithServer(const cv::Mat& image, ServerProcessingResult* server_result);
    void PublishProcessingResult(std_msgs::msg::Header header,
                                 const ServerProcessingResult& server_result);
    void WriteDebugImages(const ServerProcessingResult& server_result);
    void UpdateFps();
    void PublishStatus(bool server_connected, bool camera_active, int error_code, const std::string& error_message);
    rcl_interfaces::msg::SetParametersResult OnSetParameters(
        const std::vector<rclcpp::Parameter>& parameters);
    
};

}