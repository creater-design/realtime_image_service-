#include "realtime_image_service/image_server_config.hpp"

#include <exception>
#include <sstream>
#include <string>

#include <yaml-cpp/yaml.h>

namespace ris
{
    namespace
    {
        template <typename T>
        void ReadOptionalScalar(const YAML::Node& root,
                                const std::string& section_name,
                                const std::string& key,
                                T* value)
        {
            if (!value)
            {
                return;
            }

            const YAML::Node section = root[section_name];
            if (!section || !section[key])
            {
                return;
            }

            *value = section[key].as<T>();
        }

        bool ReadPort(const YAML::Node& root,
                      const std::string& section_name,
                      const std::string& key,
                      uint16_t* port,
                      std::string* error_message)
        {
            if (!port)
            {
                return false;
            }

            const YAML::Node section = root[section_name];
            if (!section || !section[key])
            {
                return true;
            }

            const int value = section[key].as<int>();
            if (value <= 0 || value > 65535)
            {
                if (error_message)
                {
                    *error_message = section_name + "." + key + " must be in range 1..65535";
                }
                return false;
            }

            *port = static_cast<uint16_t>(value);
            return true;
        }
    }

    bool LoadImageServerConfigFromYaml(const std::string& path,
                                       ImageServerConfig* config,
                                       std::string* error_message)
    {
        if (!config)
        {
            if (error_message) *error_message = "ImageServerConfig pointer is null";
            return false;
        }

        try
        {
            const YAML::Node root = YAML::LoadFile(path);

            if (!ReadPort(root, "image_server", "port", &config->port, error_message) ||
                !ReadPort(root, "image_server", "depth_port", &config->depth_port, error_message))
            {
                return false;
            }

            ReadOptionalScalar(root, "image_server", "use_depth_server", &config->use_depth_server);
            ReadOptionalScalar(root, "image_server", "depth_host", &config->depth_host);

            ObstacleRiskConfig risk_config = config->risk_config;
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "roi_x_ratio", &risk_config.roi_x_ratio);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "roi_y_ratio", &risk_config.roi_y_ratio);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "roi_width_ratio", &risk_config.roi_width_ratio);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "roi_height_ratio", &risk_config.roi_height_ratio);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "min_valid_depth", &risk_config.min_valid_depth);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "max_valid_depth", &risk_config.max_valid_depth);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "near_depth_threshold", &risk_config.near_depth_threshold);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "stop_depth_threshold", &risk_config.stop_depth_threshold);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "slow_depth_threshold", &risk_config.slow_depth_threshold);
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "min_valid_ratio", &risk_config.min_valid_ratio);
            ReadOptionalScalar(
                root,
                "obstacle_risk_analyzer",
                "high_min_obstacle_area_ratio",
                &risk_config.high_min_obstacle_area_ratio
            );
            ReadOptionalScalar(
                root,
                "obstacle_risk_analyzer",
                "medium_min_obstacle_area_ratio",
                &risk_config.medium_min_obstacle_area_ratio
            );
            ReadOptionalScalar(
                root,
                "obstacle_risk_analyzer",
                "min_component_area_ratio",
                &risk_config.min_component_area_ratio
            );
            ReadOptionalScalar(root, "obstacle_risk_analyzer", "morph_kernel_size", &risk_config.morph_kernel_size);

            if (!ValidateObstacleRiskConfig(risk_config, error_message))
            {
                return false;
            }

            config->risk_config = risk_config;
            return true;
        }
        catch (const std::exception& e)
        {
            if (error_message)
            {
                *error_message = "failed to load image server config " + path + ": " + e.what();
            }
            return false;
        }
    }

    std::string BuildImageServerConfigLog(const ImageServerConfig& config)
    {
        const ObstacleRiskConfig& risk = config.risk_config;
        std::ostringstream oss;
        oss << "port=" << config.port
            << " use_depth_server=" << (config.use_depth_server ? "true" : "false")
            << " depth_host=" << config.depth_host
            << " depth_port=" << config.depth_port
            << " roi=(" << risk.roi_x_ratio
            << "," << risk.roi_y_ratio
            << "," << risk.roi_width_ratio
            << "," << risk.roi_height_ratio
            << ") near_depth_threshold=" << risk.near_depth_threshold
            << " stop_depth_threshold=" << risk.stop_depth_threshold
            << " slow_depth_threshold=" << risk.slow_depth_threshold;
        return oss.str();
    }
}
