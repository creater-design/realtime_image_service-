#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/TcpServer.h>

#include "realtime_image_service/depth_estimator.hpp"

namespace ris
{
    struct RequestMetrics
    {
        int64_t decode_us = 0;
        int64_t encode_us = 0;
        int64_t total_us = 0;
    };  

    struct ImageServiceStageMetrics
    {
        int64_t depth_us = 0;
    };

    class ImageTcpServer
    { 
    public:
        ImageTcpServer(muduo::net::EventLoop* loop,
                       const muduo::net::InetAddress& address,
                       bool use_depth_server,
                       std::string depth_host,
                       uint16_t depth_port);

        ~ImageTcpServer() = default;
        void start();
        
    private:
        struct ImageResponseArtifacts
        {
            std::vector<uint8_t> result_payload;
            std::size_t output_bytes = 0;
            std::size_t depth_bytes = 0;
        };

        void onConnection(const muduo::net::TcpConnectionPtr& conn);
        void onMessage(const muduo::net::TcpConnectionPtr& conn,
                       muduo::net::Buffer* buffer,
                       muduo::Timestamp receive_time);
        void HandlePacket(const muduo::net::TcpConnectionPtr& conn,
                          const std::vector<uint8_t>& payload,
                          uint32_t request_id);
        bool DecodeRequestImage(const std::vector<uint8_t>& payload,
                                cv::Mat* input,
                                RequestMetrics* request_metrics,
                                std::string* error_message) const;
        bool EstimateDepthForImage(const cv::Mat& input,
                                   cv::Mat* depth_norm,
                                   ImageServiceStageMetrics* stage_metrics,
                                   std::string* error_message);
        bool BuildResponseArtifacts(const cv::Mat& input,
                                    const cv::Mat& depth_norm,
                                    int64_t request_begin_us,
                                    RequestMetrics* request_metrics,
                                    const ImageServiceStageMetrics& stage_metrics,
                                    ImageResponseArtifacts* artifacts,
                                    std::string* error_message) const;
        void LogRequest(uint32_t request_id,
                        const cv::Mat& input,
                        const ImageResponseArtifacts& artifacts,
                        const RequestMetrics& request_metrics,
                        const ImageServiceStageMetrics& stage_metrics) const;
        void SendError(const muduo::net::TcpConnectionPtr& conn,
                       uint32_t request_id,
                       const std::string& error_message);
        std::string BuildMetricsLog(const RequestMetrics& request_metrics,
                                    const ImageServiceStageMetrics& stage_metrics) const;

        muduo::net::TcpServer server_;
        bool use_depth_server_ = true;
        std::string depth_host_;
        uint16_t depth_port_ = 18080;
        std::unique_ptr<DepthEstimator> depth_estimator_;
    };
} // namespace ris
