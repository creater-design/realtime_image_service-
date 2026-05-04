#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/TcpServer.h>
#include "realtime_image_service/image_processor.hpp"

namespace ris
{
    struct RequestMetrics
    {
        int64_t decode_us = 0;
        int64_t encode_us = 0;
        int64_t total_us = 0;
    };  
    
    class ImageTcpServer
    { 
    public:
        ImageTcpServer(muduo::net::EventLoop* loop,
                       const muduo::net::InetAddress& address,
                       bool use_cuda);

        ~ImageTcpServer() = default;
        void start();
        
    private:
        void onConnection(const muduo::net::TcpConnectionPtr& conn);
        void onMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buffer,
                       muduo::Timestamp receive_time);
        void HandlePacket(const muduo::net::TcpConnectionPtr& conn,
                          const std::vector<uint8_t>& payload,
                          uint32_t request_id);
        void SendError(const muduo::net::TcpConnectionPtr& conn,
                       uint32_t request_id,
                       const std::string& error_message);
        // const修饰的成员函数
        // 可以读成员变量
        // 不能改成员变量
        // 只能调用其他 const 成员函数
        // 不能调用非 const 成员函数
        std::string BuildMetricsLog(const RequestMetrics& request_metrics,
                                    const ProcessMetrics& process_metrics) const;

        muduo::net::TcpServer server_;
        bool use_cuda_ = true;
    };
} // namespace ris