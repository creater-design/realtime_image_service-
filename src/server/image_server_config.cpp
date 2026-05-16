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
        std::ostringstream oss;
        oss << "port=" << config.port
            << " use_depth_server=" << (config.use_depth_server ? "true" : "false")
            << " depth_host=" << config.depth_host
            << " depth_port=" << config.depth_port;
        return oss.str();
    }
}
