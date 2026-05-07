#include <cstdint>
#include <string>
#include "realtime_image_service/image_tcp_server.hpp"  
#include "realtime_image_service/logger.hpp"

int main(int argc, char const *argv[])
{
    uint16_t port = 9999; // 默认端口号
    bool use_cuda = true;
    bool use_depth_server = true;
    std::string depth_host = "127.0.0.1";
    uint16_t depth_port = 18080;

    for (int i = 1; i < argc; ++i) 
    {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
        {
            // std::stoi() 把字符串转成整数
            port = std::stoi(argv[++i]);
        }
        else if (arg == "--cpu")
        {
            use_cuda = false;
        }
        else if (arg == "--no-depth-server")
        {
            use_depth_server = false;
        }
        else if (arg == "--depth-host" && i + 1 < argc)
        {
            depth_host = argv[++i];
        }
        else if (arg == "--depth-port" && i + 1 < argc)
        {
            depth_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }

    muduo::net::EventLoop loop;
    muduo::net::InetAddress address(port);
    ris::ImageTcpServer server(&loop, address, use_cuda, use_depth_server, depth_host, depth_port);
    RIS_LOG_INFO("image_server listening on port " + std::to_string(port));
    server.start(); // 启动服务器，开始监听端口，进入事件循环
    loop.loop(); // 启动事件循环，让程序一直卡在这里，不停地监听网络事件，然后分发给对应的回调函数。
    return 0;
}
