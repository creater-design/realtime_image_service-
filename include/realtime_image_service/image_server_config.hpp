#pragma once

#include <cstdint>
#include <string>

namespace ris
{
    struct ImageServerConfig
    {
        uint16_t port{9999};
        bool use_depth_server{true};
        std::string depth_host{"127.0.0.1"};
        uint16_t depth_port{18080};
    };

    bool LoadImageServerConfigFromYaml(const std::string& path,
                                       ImageServerConfig* config,
                                       std::string* error_message);

    std::string BuildImageServerConfigLog(const ImageServerConfig& config);
}
