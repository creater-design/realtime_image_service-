#include <cctype>
#include <memory>
#include <string>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

namespace
{
    bool ExtractJsonStringValue(const std::string& json,
                                const std::string& key,
                                std::string* value)
    {
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

        std::size_t pos = colon_pos + 1;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        {
            ++pos;
        }

        if (pos >= json.size() || json[pos] != '"')
        {
            return false;
        }

        ++pos;
        std::string parsed;
        bool escaped = false;
        for (; pos < json.size(); ++pos)
        {
            const char ch = json[pos];
            if (escaped)
            {
                parsed.push_back(ch);
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
                *value = parsed;
                return true;
            }

            parsed.push_back(ch);
        }

        return false;
    }
}

class SafetyControllerNode : public rclcpp::Node
{
public:
    SafetyControllerNode()
    : Node("safety_controller_node")
    {
        low_speed_ = declare_parameter<double>("low_speed", 0.20);
        medium_speed_ = declare_parameter<double>("medium_speed", 0.05);
        stop_on_unknown_ = declare_parameter<bool>("stop_on_unknown", true);

        cmd_vel_pub_ = create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);

        obstacle_sub_ = create_subscription<std_msgs::msg::String>(
            "/obstacle_result",
            10,
            std::bind(&SafetyControllerNode::OnObstacleResult, this, std::placeholders::_1)
        );
    }

private:
    void OnObstacleResult(const std_msgs::msg::String::SharedPtr msg)
    {
        std::string risk_level;
        if (!ExtractJsonStringValue(msg->data, "risk_level", &risk_level))
        {
            risk_level = "unknown";
        }

        geometry_msgs::msg::Twist cmd;

        if (risk_level == "high")
        {
            cmd.linear.x = 0.0;
            cmd.angular.z = 0.0;
        }
        else if (risk_level == "medium")
        {
            cmd.linear.x = medium_speed_;
            cmd.angular.z = 0.0;
        }
        else if (risk_level == "low")
        {
            cmd.linear.x = low_speed_;
            cmd.angular.z = 0.0;
        }
        else
        {
            cmd.linear.x = stop_on_unknown_ ? 0.0 : low_speed_;
            cmd.angular.z = 0.0;
        }

        cmd_vel_pub_->publish(cmd);

        RCLCPP_INFO_THROTTLE(
            get_logger(),
            *get_clock(),
            1000,
            "risk_level=%s cmd_vel.linear.x=%.3f",
            risk_level.c_str(),
            cmd.linear.x
        );
    }

    double low_speed_{0.20};
    double medium_speed_{0.05};
    bool stop_on_unknown_{true};

    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr obstacle_sub_;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SafetyControllerNode>());
    rclcpp::shutdown();
    return 0;
}
