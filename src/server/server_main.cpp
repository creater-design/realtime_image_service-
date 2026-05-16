#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "realtime_image_service/image_server_config.hpp"
#include "realtime_image_service/image_tcp_server.hpp"  
#include "realtime_image_service/logger.hpp"

namespace
{
    void PrintUsage(const char* program)
    {
        std::cout
            << "Usage: " << program << " [options]\n"
            << "Options:\n"
            << "  --config <path>        YAML config file\n"
            << "  --port <port>          image_server listen port\n"
            << "  --no-depth-server      use pseudo depth instead of depth_server\n"
            << "  --depth-host <host>    depth_server host\n"
            << "  --depth-port <port>    depth_server port\n";
    }

    bool ParsePort(const std::string& text, uint16_t* port, std::string* error_message)
    {
        if (!port)
        {
            return false;
        }

        int value = 0;
        try
        {
            value = std::stoi(text);
        }
        catch (const std::exception&)
        {
            if (error_message)
            {
                *error_message = "invalid port: " + text;
            }
            return false;
        }
        if (value <= 0 || value > 65535)
        {
            if (error_message)
            {
                *error_message = "port out of range: " + text;
            }
            return false;
        }

        *port = static_cast<uint16_t>(value);
        return true;
    }
}

int main(int argc, char const *argv[])
{
    ris::ImageServerConfig config;
    std::string config_path;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc)
        {
            config_path = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    if (!config_path.empty())
    {
        std::string error_message;
        if (!ris::LoadImageServerConfigFromYaml(config_path, &config, &error_message))
        {
            RIS_LOG_ERROR(error_message);
            return 1;
        }
    }

    // Command-line flags intentionally override YAML. This lets run scripts keep a
    // stable config file while still changing ports or backend mode for quick tests.
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        std::string error_message;
        if (arg == "--config" && i + 1 < argc)
        {
            ++i;
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            if (!ParsePort(argv[++i], &config.port, &error_message))
            {
                RIS_LOG_ERROR(error_message);
                return 1;
            }
        }
        else if (arg == "--no-depth-server")
        {
            config.use_depth_server = false;
        }
        else if (arg == "--depth-host" && i + 1 < argc)
        {
            config.depth_host = argv[++i];
        }
        else if (arg == "--depth-port" && i + 1 < argc)
        {
            if (!ParsePort(argv[++i], &config.depth_port, &error_message))
            {
                RIS_LOG_ERROR(error_message);
                return 1;
            }
        }
        else if (arg == "--help" || arg == "-h")
        {
            // Already handled before config loading.
        }
        else
        {
            RIS_LOG_ERROR("unknown or incomplete argument: " + arg);
            PrintUsage(argv[0]);
            return 1;
        }
    }

    muduo::net::EventLoop loop;
    muduo::net::InetAddress address(config.port);
    ris::ImageTcpServer server(
        &loop,
        address,
        config.use_depth_server,
        config.depth_host,
        config.depth_port
    );
    RIS_LOG_INFO("image_server config: " + ris::BuildImageServerConfigLog(config));
    RIS_LOG_INFO("image_server listening on port " + std::to_string(config.port));
    server.start();
    // Muduo's EventLoop blocks here and dispatches connection/message callbacks.
    loop.loop();
    return 0;
}
