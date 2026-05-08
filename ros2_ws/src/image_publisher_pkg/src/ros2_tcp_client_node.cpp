#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/header.hpp>
#include <std_msgs/msg/string.hpp>

#include "realtime_image_service/tcp_image_client.hpp"

namespace
{
    std::string JsonEscape(const std::string& input)
    {
        std::ostringstream oss;
        for (const char ch : input)
        {
            switch (ch)
            {
                case '\\': oss << "\\\\"; break;
                case '"': oss << "\\\""; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default: oss << ch; break;
            }
        }
        return oss.str();
    }

    bool ExtractJsonObjectValue(const std::string& json,
                                const std::string& key,
                                std::string* value)
    {
        // The server JSON shape is controlled by this project. This lightweight
        // extractor avoids adding a JSON dependency to the ROS2 node for one field.
        if (!value)
        {
            return false;
        }

        const std::string quoted_key = "\"" + key + "\"";
        const std::size_t key_pos = json.find(quoted_key);
        if (key_pos == std::string::npos)
        {
            return false;
        }

        const std::size_t colon_pos = json.find(':', key_pos + quoted_key.size());
        if (colon_pos == std::string::npos)
        {
            return false;
        }

        const std::size_t object_begin = json.find('{', colon_pos + 1);
        if (object_begin == std::string::npos)
        {
            return false;
        }

        int depth = 0;
        bool in_string = false;
        bool escaped = false;

        for (std::size_t i = object_begin; i < json.size(); ++i)
        {
            const char ch = json[i];
            if (escaped)
            {
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (ch == '"')
            {
                in_string = !in_string;
                continue;
            }

            if (in_string)
            {
                continue;
            }

            if (ch == '{')
            {
                ++depth;
            }
            else if (ch == '}')
            {
                --depth;
                if (depth == 0)
                {
                    *value = json.substr(object_begin, i - object_begin + 1);
                    return true;
                }
            }
        }

        return false;
    }
}

class Ros2TcpClientNode : public rclcpp::Node
{
public:
    Ros2TcpClientNode()
    : Node("ros2_tcp_client_node")
    {
        host_ = declare_parameter<std::string>("host", "127.0.0.1");
        port_ = declare_parameter<uint16_t>("port", 9999);
        topic_name_ = declare_parameter<std::string>("topic_name", "/camera/image_raw");
        output_path_ = declare_parameter<std::string>("output_path", "");
        depth_output_path_ = declare_parameter<std::string>("depth_output_path", "");
        max_request_fps_.store(std::max(0.1, declare_parameter<double>("max_request_fps", 60.0)));

        subscription_ = create_subscription<sensor_msgs::msg::Image>(
            topic_name_,
            rclcpp::SensorDataQoS(),
            std::bind(&Ros2TcpClientNode::OnImage, this, std::placeholders::_1)
        );

        processed_image_pub_ = create_publisher<sensor_msgs::msg::Image>(
            "/image_service/processed_image",
            rclcpp::SensorDataQoS()
        );

        depth_image_pub_ = create_publisher<sensor_msgs::msg::Image>(
            "/image_service/depth_image",
            rclcpp::SensorDataQoS()
        );

        obstacle_result_pub_ = create_publisher<std_msgs::msg::String>(
            "/obstacle_result",
            10
        );

        metrics_pub_ = create_publisher<std_msgs::msg::String>(
            "/image_service/metrics",
            10
        );

        status_pub_ = create_publisher<std_msgs::msg::String>(
            "/image_service/status",
            10
        );

        last_fps_time_ = now();
        parameter_callback_handle_ = add_on_set_parameters_callback(
            std::bind(&Ros2TcpClientNode::OnSetParameters, this, std::placeholders::_1)
        );
        worker_running_.store(true);
        worker_thread_ = std::thread(&Ros2TcpClientNode::ProcessLoop, this);

        RCLCPP_INFO(
            get_logger(),
            "ros2_tcp_client_node started: topic=%s server=%s:%u max_request_fps=%.2f",
            topic_name_.c_str(),
            host_.c_str(),
            static_cast<unsigned>(port_),
            max_request_fps_.load()
        );
    }

    ~Ros2TcpClientNode() override
    {
        worker_running_.store(false);
        frame_cv_.notify_all();
        if (worker_thread_.joinable())
        {
            worker_thread_.join();
        }

        if (client_)
        {
            std::lock_guard<std::mutex> client_lock(client_mutex_);
            client_->Close();
        }
    }

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

    void OnImage(const sensor_msgs::msg::Image::SharedPtr msg)
    {
        try
        {
            const cv::Mat image = cv_bridge::toCvCopy(msg, "bgr8")->image.clone();
            {
                // Store only the newest frame. The model is slower than the camera,
                // so queueing every frame would increase latency with stale images.
                std::lock_guard<std::mutex> lock(frame_mutex_);
                latest_frame_ = image;
                latest_header_ = msg->header;
                ++latest_frame_sequence_;
                has_frame_ = true;
            }
            frame_cv_.notify_one();
        }
        catch (const cv::Exception& e)
        {
            RCLCPP_ERROR(get_logger(), "OpenCV error: %s", e.what());
            PublishStatus(false, false, 3, e.what());
        }
        catch (const std::exception& e)
        {
            RCLCPP_ERROR(get_logger(), "error: %s", e.what());
            PublishStatus(false, false, 4, e.what());
        }
    }

    void ProcessLoop()
    {
        auto next_allowed_time = std::chrono::steady_clock::now();
        while (worker_running_.load())
        {
            // Decouple camera subscription FPS from inference FPS. The worker samples
            // the latest frame at max_request_fps and drops older frames intentionally.
            const auto interval = std::chrono::duration<double>(1.0 / max_request_fps_.load());
            FrameSnapshot snapshot;
            if (!WaitForNextFrame(next_allowed_time, &snapshot))
            {
                continue;
            }

            const auto started_at = std::chrono::steady_clock::now();
            ProcessFrame(snapshot.image, snapshot.header);
            next_allowed_time = started_at +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(interval);
        }
    }

    bool WaitForNextFrame(std::chrono::steady_clock::time_point next_allowed_time,
                          FrameSnapshot* snapshot)
    {
        if (!snapshot)
        {
            return false;
        }

        std::unique_lock<std::mutex> lock(frame_mutex_);
        frame_cv_.wait(lock, [this]() {
            return !worker_running_.load() ||
                   (has_frame_ && latest_frame_sequence_ != processed_frame_sequence_);
        });

        if (!worker_running_.load())
        {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now < next_allowed_time)
        {
            frame_cv_.wait_until(lock, next_allowed_time, [this]() {
                return !worker_running_.load();
            });
            if (!worker_running_.load())
            {
                return false;
            }
        }

        if (!has_frame_ || latest_frame_sequence_ == processed_frame_sequence_)
        {
            return false;
        }

        snapshot->image = latest_frame_.clone();
        snapshot->header = latest_header_;
        snapshot->sequence = latest_frame_sequence_;
        processed_frame_sequence_ = latest_frame_sequence_;
        return true;
    }

    void ProcessFrame(const cv::Mat& image, const std_msgs::msg::Header& header)
    {
        ServerProcessingResult server_result;
        if (!ProcessFrameWithServer(image, &server_result))
        {
            return;
        }

        UpdateFps();
        PublishProcessingResult(header, server_result);
        WriteDebugImages(server_result);
    }

    bool ProcessFrameWithServer(const cv::Mat& image, ServerProcessingResult* server_result)
    {
        if (!server_result)
        {
            PublishStatus(false, true, 5, "server_result pointer is null");
            return false;
        }

        std::string error_message;
        std::lock_guard<std::mutex> client_lock(client_mutex_);
        if (!client_)
        {
            // TcpImageClient keeps its TCP connection after the first successful
            // request; recreating it only happens when host/port changes or errors occur.
            std::lock_guard<std::mutex> config_lock(config_mutex_);
            client_ = std::make_unique<ris::TcpImageClient>(host_, port_);
        }

        if (!client_->Connect(&error_message))
        {
            RCLCPP_ERROR_THROTTLE(
                get_logger(),
                *get_clock(),
                2000,
                "%s",
                error_message.c_str()
            );
            client_->Close();
            PublishStatus(false, true, 1, error_message);
            return false;
        }

        const uint32_t request_id = ++request_id_;
        if (!client_->SendImageWithResultAndDepth(
                image,
                &server_result->processed_image,
                &server_result->depth_image,
                &server_result->result_json,
                request_id,
                &error_message))
        {
            RCLCPP_ERROR_THROTTLE(
                get_logger(),
                *get_clock(),
                2000,
                "%s",
                error_message.c_str()
            );
            client_->Close();
            PublishStatus(false, true, 2, error_message);
            return false;
        }

        RCLCPP_INFO_THROTTLE(
            get_logger(),
            *get_clock(),
            2000,
            "request_id=%u processed_fps=%.2f result_json=%s",
            request_id,
            fps_.load(),
            server_result->result_json.c_str()
        );
        return true;
    }

    void PublishProcessingResult(std_msgs::msg::Header header,
                                 const ServerProcessingResult& server_result)
    {
        header.stamp = now();
        if (header.frame_id.empty())
        {
            header.frame_id = "camera";
        }

        auto processed_msg = cv_bridge::CvImage(
            header,
            "bgr8",
            server_result.processed_image
        ).toImageMsg();
        processed_image_pub_->publish(*processed_msg);

        if (!server_result.depth_image.empty())
        {
            auto depth_msg = cv_bridge::CvImage(
                header,
                "bgr8",
                server_result.depth_image
            ).toImageMsg();
            depth_image_pub_->publish(*depth_msg);
        }

        std_msgs::msg::String obstacle_msg;
        obstacle_msg.data = server_result.result_json;
        obstacle_result_pub_->publish(obstacle_msg);

        std::string metrics_json;
        if (!ExtractJsonObjectValue(server_result.result_json, "metrics", &metrics_json))
        {
            metrics_json = "{}";
        }

        std_msgs::msg::String metrics_msg;
        metrics_msg.data = metrics_json;
        metrics_pub_->publish(metrics_msg);

        PublishStatus(true, true, 0, "");
    }

    void WriteDebugImages(const ServerProcessingResult& server_result)
    {
        std::string output_path;
        std::string depth_output_path;
        {
            std::lock_guard<std::mutex> config_lock(config_mutex_);
            output_path = output_path_;
            depth_output_path = depth_output_path_;
        }

        // Empty paths disable disk writes. This matters for real-time tests because
        // writing JPEG files every frame can become a visible latency source.
        if (!output_path.empty() &&
            !cv::imwrite(output_path, server_result.processed_image))
        {
            RCLCPP_ERROR_THROTTLE(
                get_logger(),
                *get_clock(),
                2000,
                "failed to write output image: %s",
                output_path.c_str()
            );
        }

        if (!depth_output_path.empty() &&
            !server_result.depth_image.empty() &&
            !cv::imwrite(depth_output_path, server_result.depth_image))
        {
            RCLCPP_ERROR_THROTTLE(
                get_logger(),
                *get_clock(),
                2000,
                "failed to write depth image: %s",
                depth_output_path.c_str()
            );
        }
    }

    void UpdateFps()
    {
        ++frames_since_fps_update_;

        const rclcpp::Time current_time = now();
        const double elapsed = (current_time - last_fps_time_).seconds();
        if (elapsed >= 1.0)
        {
            fps_.store(static_cast<double>(frames_since_fps_update_) / elapsed);
            frames_since_fps_update_ = 0;
            last_fps_time_ = current_time;
        }
    }

    void PublishStatus(bool server_connected,
                       bool camera_active,
                       int error_code,
                       const std::string& error_message)
    {
        std::ostringstream oss;
        oss << "{";
        oss << "\"server_connected\":" << (server_connected ? "true" : "false") << ",";
        oss << "\"camera_active\":" << (camera_active ? "true" : "false") << ",";
        oss << "\"fps\":" << fps_.load() << ",";
        oss << "\"max_request_fps\":" << max_request_fps_.load() << ",";
        oss << "\"error_code\":" << error_code << ",";
        oss << "\"error_message\":\"" << JsonEscape(error_message) << "\"";
        oss << "}";

        std_msgs::msg::String status_msg;
        status_msg.data = oss.str();
        status_pub_->publish(status_msg);
    }

    rcl_interfaces::msg::SetParametersResult OnSetParameters(
        const std::vector<rclcpp::Parameter>& parameters)
    {
        rcl_interfaces::msg::SetParametersResult result;
        result.successful = true;

        for (const auto& parameter : parameters)
        {
            const std::string& name = parameter.get_name();
            if (name == "host" || name == "topic_name")
            {
                if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_STRING ||
                    parameter.as_string().empty())
                {
                    result.successful = false;
                    result.reason = name + " must be a non-empty string";
                    return result;
                }
            }
            else if (name == "output_path" || name == "depth_output_path")
            {
                if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_STRING)
                {
                    result.successful = false;
                    result.reason = name + " must be a string";
                    return result;
                }
            }
            else if (name == "port")
            {
                if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_INTEGER ||
                    parameter.as_int() <= 0 ||
                    parameter.as_int() > 65535)
                {
                    result.successful = false;
                    result.reason = "port must be in range 1..65535";
                    return result;
                }
            }
            else if (name == "max_request_fps")
            {
                if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE ||
                    parameter.as_double() <= 0.0)
                {
                    result.successful = false;
                    result.reason = "max_request_fps must be a positive double";
                    return result;
                }
            }
        }

        bool reset_client = false;
        bool recreate_subscription = false;
        {
            std::lock_guard<std::mutex> config_lock(config_mutex_);
            for (const auto& parameter : parameters)
            {
                const std::string& name = parameter.get_name();
                if (name == "host")
                {
                    host_ = parameter.as_string();
                    reset_client = true;
                }
                else if (name == "port")
                {
                    port_ = static_cast<uint16_t>(parameter.as_int());
                    reset_client = true;
                }
                else if (name == "topic_name")
                {
                    topic_name_ = parameter.as_string();
                    recreate_subscription = true;
                }
                else if (name == "output_path")
                {
                    output_path_ = parameter.as_string();
                }
                else if (name == "depth_output_path")
                {
                    depth_output_path_ = parameter.as_string();
                }
                else if (name == "max_request_fps")
                {
                    max_request_fps_.store(std::max(0.1, parameter.as_double()));
                    frame_cv_.notify_one();
                }
            }
        }

        if (recreate_subscription)
        {
            // Topic changes need a new subscription; the worker thread can keep using
            // the latest-frame cache without knowing where frames came from.
            subscription_ = create_subscription<sensor_msgs::msg::Image>(
                topic_name_,
                rclcpp::SensorDataQoS(),
                std::bind(&Ros2TcpClientNode::OnImage, this, std::placeholders::_1)
            );
        }

        if (reset_client)
        {
            // Force the next request to open a connection to the updated server.
            std::lock_guard<std::mutex> client_lock(client_mutex_);
            if (client_)
            {
                client_->Close();
                client_.reset();
            }
        }

        RCLCPP_INFO(
            get_logger(),
            "updated parameters: host=%s port=%u topic_name=%s max_request_fps=%.2f",
            host_.c_str(),
            static_cast<unsigned>(port_),
            topic_name_.c_str(),
            max_request_fps_.load()
        );

        return result;
    }

    std::string host_;
    uint16_t port_{9999};
    std::string topic_name_;
    std::string output_path_;
    std::string depth_output_path_;
    std::atomic<double> max_request_fps_{60.0};
    uint32_t request_id_{0};
    std::atomic<double> fps_{0.0};
    int frames_since_fps_update_{0};
    rclcpp::Time last_fps_time_;
    std::atomic<bool> worker_running_{false};
    std::thread worker_thread_;
    std::mutex config_mutex_;
    std::mutex client_mutex_;
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    cv::Mat latest_frame_;
    std_msgs::msg::Header latest_header_;
    bool has_frame_{false};
    uint64_t latest_frame_sequence_{0};
    uint64_t processed_frame_sequence_{0};
    std::unique_ptr<ris::TcpImageClient> client_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr processed_image_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr depth_image_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr obstacle_result_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr metrics_pub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr parameter_callback_handle_;
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Ros2TcpClientNode>());
    rclcpp::shutdown();
    return 0;
}
