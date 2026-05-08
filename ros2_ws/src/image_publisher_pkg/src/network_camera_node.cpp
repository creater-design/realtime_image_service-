#include "image_publisher_pkg/network_camera_node.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/imgproc.hpp>
#include <std_msgs/msg/header.hpp>

namespace image_publisher_pkg
{

NetworkCameraNode::NetworkCameraNode()
: Node("network_camera_node")
{
    stream_url_ = declare_parameter<std::string>(
        "stream_url",
        "http://192.168.1.12:8080/video"
    );
    topic_ = declare_parameter<std::string>("topic", "/camera/image_raw");
    fps_ = declare_parameter<double>("fps", 15.0);
    image_width_ = declare_parameter<int>("image_width", 640);
    image_height_ = declare_parameter<int>("image_height", 480);
    reconnect_interval_ = declare_parameter<double>("reconnect_interval", 2.0);
    frame_id_ = declare_parameter<std::string>("frame_id", "network_camera");

    if (fps_ <= 0.0)
    {
        RCLCPP_WARN(
            get_logger(),
            "fps must be > 0, reset to 15.0"
        );
        fps_ = 15.0;
    }

    if (reconnect_interval_ <= 0.0)
    {
        RCLCPP_WARN(
            get_logger(),
            "reconnect_interval must be > 0, reset to 2.0"
        );
        reconnect_interval_ = 2.0;
    }

    RCLCPP_INFO(
        get_logger(),
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

    image_pub_ = create_publisher<sensor_msgs::msg::Image>(topic_, 10);

    openStream();
    resetTimer();

    parameter_callback_handle_ = add_on_set_parameters_callback(
        std::bind(&NetworkCameraNode::onSetParameters, this, std::placeholders::_1)
    );
}

NetworkCameraNode::~NetworkCameraNode()
{
    closeStream();
}

bool NetworkCameraNode::openStream()
{
    closeStream();

    if (stream_url_.empty())
    {
        RCLCPP_ERROR(get_logger(), "stream_url is empty");
        return false;
    }

    RCLCPP_INFO(get_logger(), "opening network camera stream: %s", stream_url_.c_str());

    if (!cap_.open(stream_url_))
    {
        RCLCPP_ERROR_THROTTLE(
            get_logger(),
            *get_clock(),
            5000,
            "failed to open network camera stream: %s",
            stream_url_.c_str()
        );
        return false;
    }

    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);

    RCLCPP_INFO(get_logger(), "network camera stream opened");
    return true;
}

void NetworkCameraNode::closeStream()
{
    if (cap_.isOpened())
    {
        cap_.release();
    }
}

void NetworkCameraNode::resetTimer()
{
    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / fps_)
    );

    timer_ = create_wall_timer(
        period,
        std::bind(&NetworkCameraNode::onTimer, this)
    );
}

void NetworkCameraNode::tryReconnect()
{
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - last_reconnect_attempt_).count();
    if (last_reconnect_attempt_ != std::chrono::steady_clock::time_point::min() &&
        elapsed < reconnect_interval_)
    {
        return;
    }

    last_reconnect_attempt_ = now;

    RCLCPP_WARN(
        get_logger(),
        "trying to reconnect network camera stream"
    );
    openStream();
}

void NetworkCameraNode::onTimer()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!cap_.isOpened())
    {
        tryReconnect();
        return;
    }

    if (!cap_.read(frame_) || frame_.empty())
    {
        RCLCPP_WARN_THROTTLE(
            get_logger(),
            *get_clock(),
            2000,
            "failed to read frame from network camera stream"
        );
        closeStream();
        tryReconnect();
        return;
    }

    cv::Mat publish_frame = frame_;
    if (image_width_ > 0 && image_height_ > 0 &&
        (frame_.cols != image_width_ || frame_.rows != image_height_))
    {
        cv::resize(frame_, publish_frame, cv::Size(image_width_, image_height_));
    }

    std_msgs::msg::Header header;
    header.stamp = now();
    header.frame_id = frame_id_;

    auto msg = cv_bridge::CvImage(header, "bgr8", publish_frame).toImageMsg();
    image_pub_->publish(*msg);
}

rcl_interfaces::msg::SetParametersResult NetworkCameraNode::onSetParameters(
    const std::vector<rclcpp::Parameter>& parameters)
{
    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;

    for (const auto& parameter : parameters)
    {
        const std::string& name = parameter.get_name();
        if (name == "stream_url" || name == "topic" || name == "frame_id")
        {
            if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_STRING ||
                parameter.as_string().empty())
            {
                result.successful = false;
                result.reason = name + " must be a non-empty string";
                return result;
            }
        }
        else if (name == "fps" || name == "reconnect_interval")
        {
            if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                parameter.as_double() <= 0.0)
            {
                result.successful = false;
                result.reason = name + " must be a positive double";
                return result;
            }
        }
        else if (name == "image_width" || name == "image_height")
        {
            if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_INTEGER ||
                parameter.as_int() < 0)
            {
                result.successful = false;
                result.reason = name + " must be an integer >= 0";
                return result;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    bool reopen_stream = false;
    bool recreate_publisher = false;
    bool recreate_timer = false;

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
            image_width_ = static_cast<int>(parameter.as_int());
        }
        else if (name == "image_height")
        {
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

    if (recreate_publisher)
    {
        image_pub_ = create_publisher<sensor_msgs::msg::Image>(topic_, 10);
    }

    if (recreate_timer)
    {
        resetTimer();
    }

    if (reopen_stream)
    {
        last_reconnect_attempt_ = std::chrono::steady_clock::time_point::min();
        openStream();
    }

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

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<image_publisher_pkg::NetworkCameraNode>());
    rclcpp::shutdown();
    return 0;
}
