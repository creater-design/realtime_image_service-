#include <cstring>
#include <vector>
#include <opencv2/opencv.hpp>

#include "realtime_image_service/image_tcp_server.hpp"
#include "realtime_image_service/image_codec.hpp"
#include "realtime_image_service/image_processor.hpp"
#include "realtime_image_service/logger.hpp"
#include "realtime_image_service/protocol.hpp"
#include "realtime_image_service/obstacle_risk_analyzer.hpp"

namespace ris
{
    // muduo::net::EventLoop* loop 
    // muduo 的事件循环：
    // 监听 socket 事件
    // 有新连接时通知你
    // 有消息可读时通知你
    // 把事件分发给对应回调函数
    ImageTcpServer::ImageTcpServer(muduo::net::EventLoop* loop,
                                   const muduo::net::InetAddress& address, // 服务器要监听的地址和端口
                                   bool use_cuda)
    : server_(loop, address, "ImageTcpServer")
    , use_cuda_(use_cuda)
    {
        // 以后只要有连接状态变化，就调用我这个对象的 onConnection 成员函数。
        server_.setConnectionCallback(
            std::bind(&ImageTcpServer::onConnection, this, std::placeholders::_1));
        // 以后只要有消息可读，就调用我这个对象的 onMessage 成员函数。
        server_.setMessageCallback(
            std::bind(&ImageTcpServer::onMessage, this, std::placeholders::_1,
                      std::placeholders::_2, std::placeholders::_3));

        server_.setThreadNum(4);
    }

    void ImageTcpServer::start()
    {
        server_.start();
        RIS_LOG_INFO(std::string("ImageTcpServer started on ") + (use_cuda_ ? "CUDA" : "CPU"));
    }

    void ImageTcpServer::onConnection(const muduo::net::TcpConnectionPtr& conn)
    {
        // 只要 buffer 里剩余可读字节数 至少有一个完整消息头那么大，就尝试拆一条消息
        if (conn->connected()) 
        {
            RIS_LOG_INFO("new connection from " + conn->peerAddress().toIpPort());
        } 
        else 
        {
            RIS_LOG_INFO("connection closed: " + conn->peerAddress().toIpPort());
        }
    }

    void ImageTcpServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
                                   muduo::net::Buffer* buffer,
                                   muduo::Timestamp time)
    {
          while (buffer->readableBytes() >= kHeaderSize) 
          {
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(buffer->peek());
            auto header_opt = DeserializeHeader(raw, kHeaderSize);
            if (!header_opt.has_value()) 
            {
                RIS_LOG_WARN("invalid header, close connection");
                conn->shutdown();
                return;
            }

            const MessageHeader header = header_opt.value();
            // 如果缓冲区的可读字节数 不足以组成一个完整消息（消息头 + 消息体），就等下一次 onMessage 再来处理
            if (buffer->readableBytes() < kHeaderSize + header.payload_size) 
            {
                RIS_LOG_WARN("incomplete message, wait for more data");
                return;
            }

            // 现在已经确认完整包到了，那就把头部这 kHeaderSize 个字节从缓冲区消费掉。
            buffer->retrieve(kHeaderSize);
            std::vector<uint8_t> payload(header.payload_size);
            // 如果消息体长度大于 0
            // 就从 buffer 当前可读位置把消息体拷贝到 payload
            // 然后把这些字节从 buffer 中消费掉
            if (header.payload_size > 0) 
            {
                std::memcpy(payload.data(), buffer->peek(), header.payload_size);
                buffer->retrieve(header.payload_size);
            }

            if (static_cast<MessageType>(header.msg_type) != MessageType::kImageRequest) 
            {
                SendError(conn, header.request_id, "unsupported msg_type");
                continue;
            }

            HandlePacket(conn, payload, header.request_id);
        }


    }

    void ImageTcpServer::HandlePacket(const muduo::net::TcpConnectionPtr& conn,
                                      const std::vector<uint8_t>& payload,
                                      uint32_t request_id) 
    {
        const int64_t request_begin = NowUs();
        cv::Mat input;
        std::string error_message;
        RequestMetrics request_metrics; // 请求的指标
        ProcessMetrics process_metrics; // 处理的指标

        const int64_t decode_begin = NowUs();
        if (!DecodeImage(payload, &input, &error_message)) 
        {
            SendError(conn, request_id, error_message);
            return;
        }
        request_metrics.decode_us = NowUs() - decode_begin;

        const int64_t preprocess_begin = NowUs();

        cv::Mat gray;
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);

        cv::Mat depth_norm;
        gray.convertTo(depth_norm, CV_32FC1, 1.0 / 255.0);

        // 临时伪深度：让暗区域更近、亮区域更远。
        // 注意：这不是最终单目深度，只是为了先验证 ROI 风险分析链路。
        depth_norm = 1.0f - depth_norm;

        process_metrics.preprocess_us = NowUs() - preprocess_begin;

        ObstacleRiskAnalyzer analyzer;
        ObstacleResult obstacle_result;

        const int64_t risk_begin = NowUs();
        if (!analyzer.Analyze(depth_norm, &obstacle_result, &error_message))
        {
            SendError(conn, request_id, error_message);
            return;
        }
        process_metrics.compute_us = NowUs() - risk_begin;

        cv::Mat output = input.clone();
        analyzer.DrawResult(obstacle_result, &output);

        std::vector<uint8_t> encoded;
        const int64_t encode_begin = NowUs();
        if (!EncodeJpeg(output, &encoded, 90, &error_message)) 
        {
            SendError(conn, request_id, error_message);
            return;
        }
        request_metrics.encode_us = NowUs() - encode_begin;
        request_metrics.total_us = NowUs() - request_begin;

        std::vector<uint8_t> packet = BuildPacket(MessageType::kImageResponse, request_id, encoded);
        // packet.data() 返回 vector 底层连续内存的首地址。
        conn->send(packet.data(), static_cast<int>(packet.size()));

        RIS_LOG_INFO("request_id=" + std::to_string(request_id) +
            " input=" + std::to_string(input.cols) + "x" + std::to_string(input.rows) +
            " output_bytes=" + std::to_string(encoded.size()) +
            " risk_level=" + obstacle_result.risk_level +
            " action=" + obstacle_result.suggest_action +
            " near_ratio=" + std::to_string(obstacle_result.near_ratio) +
            " confidence=" + std::to_string(obstacle_result.confidence) +
            " " + BuildMetricsLog(request_metrics, process_metrics));

    }

    void ImageTcpServer::SendError(const muduo::net::TcpConnectionPtr& conn,
                               uint32_t request_id,
                               const std::string& error_message) 
    {
        RIS_LOG_ERROR("request: " + std::to_string(request_id) + " failed: " + error_message);
        std::vector<uint8_t> packet = BuildErrorPacket(request_id, error_message);
        conn->send(packet.data(), static_cast<int>(packet.size()));
    }

    std::string ImageTcpServer::BuildMetricsLog(const RequestMetrics& request_metrics,
                                            const ProcessMetrics& process_metrics) const 
    {
        std::string backend = use_cuda_ ? "cuda" : "cpu";
        std::string log = "backend=" + backend +
                            " decode_us=" + std::to_string(request_metrics.decode_us) +
                            " preprocess_us=" + std::to_string(process_metrics.preprocess_us) +
                            " compute_us=" + std::to_string(process_metrics.compute_us) +
                            " encode_us=" + std::to_string(request_metrics.encode_us) +
                            " total_us=" + std::to_string(request_metrics.total_us);
        if (use_cuda_) 
        {
            log += " h2d_us=" + std::to_string(process_metrics.transfer_h2d_us) +
                " d2h_us=" + std::to_string(process_metrics.transfer_d2h_us);
        }
        return log;
    }
} // namespace ris
