#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include <opencv2/opencv.hpp>

#include "realtime_image_service/image_tcp_server.hpp"
#include "realtime_image_service/image_codec.hpp"
#include "realtime_image_service/logger.hpp"
#include "realtime_image_service/protocol.hpp"
#include "realtime_image_service/result_json.hpp"
#include "realtime_image_service/result_payload.hpp"

namespace ris
{
    namespace
    {
        cv::Mat BuildDepthVisualization(const cv::Mat& depth_norm)
        {
            // depth_norm uses smaller values for closer regions. The display is inverted
            // so nearby content appears hotter in the color map.
            cv::Mat depth_float;
            if (depth_norm.type() == CV_32FC1)
            {
                depth_float = depth_norm;
            }
            else
            {
                depth_norm.convertTo(depth_float, CV_32FC1, 1.0 / 255.0);
            }

            cv::Mat display = 1.0f - depth_float;
            cv::patchNaNs(display, 0.0);
            cv::threshold(display, display, 1.0, 1.0, cv::THRESH_TRUNC);
            cv::threshold(display, display, 0.0, 0.0, cv::THRESH_TOZERO);

            cv::Mat depth_u8;
            display.convertTo(depth_u8, CV_8UC1, 255.0);

            cv::Mat depth_color;
            cv::applyColorMap(depth_u8, depth_color, cv::COLORMAP_TURBO);
            return depth_color;
        }
    }

    ImageTcpServer::ImageTcpServer(muduo::net::EventLoop* loop,
                                   const muduo::net::InetAddress& address,
                                   bool use_depth_server,
                                   std::string depth_host,
                                   uint16_t depth_port)
    : server_(loop, address, "ImageTcpServer")
    , use_depth_server_(use_depth_server)
    , depth_host_(std::move(depth_host))
    , depth_port_(depth_port)
    {
        if (use_depth_server_)
        {
            depth_estimator_ = std::make_unique<DepthEstimator>(depth_host_, depth_port_);
        }

        // Muduo owns socket polling; this class only handles connection events and
        // complete application-level image packets.
        server_.setConnectionCallback(
            std::bind(&ImageTcpServer::onConnection, this, std::placeholders::_1));
        server_.setMessageCallback(
            std::bind(&ImageTcpServer::onMessage, this, std::placeholders::_1,
                      std::placeholders::_2, std::placeholders::_3));

        server_.setThreadNum(4);
    }

    void ImageTcpServer::start()
    {
        server_.start();
        RIS_LOG_INFO(std::string("ImageTcpServer started depth_backend=") +
                     (use_depth_server_ ? (depth_host_ + ":" + std::to_string(depth_port_)) : "pseudo_depth"));
    }

    void ImageTcpServer::onConnection(const muduo::net::TcpConnectionPtr& conn)
    {
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
          (void)time;

          // TCP is a byte stream. A single onMessage callback may contain half a
          // packet, one packet, or multiple packets. The loop below uses the fixed
          // protocol header and payload_size to restore application message boundaries.
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
            if (buffer->readableBytes() < kHeaderSize + header.payload_size) 
            {
                RIS_LOG_WARN("incomplete message, wait for more data");
                return;
            }

            buffer->retrieve(kHeaderSize);
            std::vector<uint8_t> payload(header.payload_size);
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
        cv::Mat depth_norm;
        std::string error_message;
        RequestMetrics request_metrics;
        ImageServiceStageMetrics stage_metrics;

        // Server pipeline: decode request -> estimate depth -> render depth image ->
        // package images and metrics into one response payload.
        if (!DecodeRequestImage(payload, &input, &request_metrics, &error_message))
        {
            SendError(conn, request_id, error_message);
            return;
        }

        if (!EstimateDepthForImage(input, &depth_norm, &stage_metrics, &error_message))
        {
            SendError(conn, request_id, error_message);
            return;
        }

        ImageResponseArtifacts artifacts;
        if (!BuildResponseArtifacts(input,
                                    depth_norm,
                                    request_begin,
                                    &request_metrics,
                                    stage_metrics,
                                    &artifacts,
                                    &error_message))
        {
            SendError(conn, request_id, error_message);
            return;
        }

        std::vector<uint8_t> packet = BuildPacket(
            MessageType::kImageResponse,
            request_id,
            artifacts.result_payload
        );
        conn->send(packet.data(), static_cast<int>(packet.size()));

        LogRequest(request_id, input, artifacts, request_metrics, stage_metrics);
    }

    bool ImageTcpServer::DecodeRequestImage(const std::vector<uint8_t>& payload,
                                            cv::Mat* input,
                                            RequestMetrics* request_metrics,
                                            std::string* error_message) const
    {
        if (!input || !request_metrics)
        {
            if (error_message) *error_message = "DecodeRequestImage received null output";
            return false;
        }

        const int64_t decode_begin = NowUs();
        if (!DecodeImage(payload, input, error_message))
        {
            return false;
        }

        request_metrics->decode_us = NowUs() - decode_begin;
        return true;
    }

    bool ImageTcpServer::EstimateDepthForImage(const cv::Mat& input,
                                               cv::Mat* depth_norm,
                                               ImageServiceStageMetrics* stage_metrics,
                                               std::string* error_message)
    {
        if (!depth_norm || !stage_metrics)
        {
            if (error_message) *error_message = "EstimateDepthForImage received null output";
            return false;
        }

        const int64_t depth_begin = NowUs();
        if (use_depth_server_)
        {
            if (!depth_estimator_)
            {
                if (error_message) *error_message = "depth estimator is not initialized";
                return false;
            }

            if (!depth_estimator_->EstimateDepth(input, depth_norm, error_message))
            {
                return false;
            }
        }
        else
        {
            // Pseudo depth is only for connectivity/debug testing when the model
            // service is unavailable. Production runs should use Depth Anything V2.
            cv::Mat gray;
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
            gray.convertTo(*depth_norm, CV_32FC1, 1.0 / 255.0);
            *depth_norm = 1.0f - *depth_norm;
        }

        stage_metrics->depth_us = NowUs() - depth_begin;
        return true;
    }

    bool ImageTcpServer::BuildResponseArtifacts(const cv::Mat& input,
                                                const cv::Mat& depth_norm,
                                                int64_t request_begin_us,
                                                RequestMetrics* request_metrics,
                                                const ImageServiceStageMetrics& stage_metrics,
                                                ImageResponseArtifacts* artifacts,
                                                std::string* error_message) const
    {
        if (!request_metrics || !artifacts)
        {
            if (error_message) *error_message = "BuildResponseArtifacts received null output";
            return false;
        }

        *artifacts = ImageResponseArtifacts{};

        // The response contains the original image, the rendered depth view, and
        // machine-readable timing metrics for ROS2 observability.
        cv::Mat output = input.clone();
        cv::Mat depth_visual = BuildDepthVisualization(depth_norm);

        std::vector<uint8_t> encoded;
        std::vector<uint8_t> depth_encoded;
        const int64_t encode_begin = NowUs();
        if (!EncodeJpeg(output, &encoded, 90, error_message)) 
        {
            return false;
        }

        if (!EncodeJpeg(depth_visual, &depth_encoded, 90, error_message))
        {
            return false;
        }

        request_metrics->encode_us = NowUs() - encode_begin;
        request_metrics->total_us = NowUs() - request_begin_us;

        ImageServiceMetrics service_metrics;
        service_metrics.backend = use_depth_server_ ? "depth_anything_v2" : "pseudo_depth";
        service_metrics.decode_us = request_metrics->decode_us;
        service_metrics.depth_us = stage_metrics.depth_us;
        service_metrics.encode_us = request_metrics->encode_us;
        service_metrics.total_us = request_metrics->total_us;

        const std::string json = BuildImageServiceResultJson(
            input.cols,
            input.rows,
            !depth_encoded.empty(),
            service_metrics
        );

        artifacts->result_payload = PackResultPayloadWithDepth(json, encoded, depth_encoded);
        artifacts->output_bytes = encoded.size();
        artifacts->depth_bytes = depth_encoded.size();
        return true;
    }

    void ImageTcpServer::LogRequest(uint32_t request_id,
                                    const cv::Mat& input,
                                    const ImageResponseArtifacts& artifacts,
                                    const RequestMetrics& request_metrics,
                                    const ImageServiceStageMetrics& stage_metrics) const
    {
        if (request_id != 1 && request_id % 60 != 0)
        {
            return;
        }

        RIS_LOG_INFO("request_id=" + std::to_string(request_id) +
            " input=" + std::to_string(input.cols) + "x" + std::to_string(input.rows) +
            " output_bytes=" + std::to_string(artifacts.output_bytes) +
            " depth_bytes=" + std::to_string(artifacts.depth_bytes) +
            " " + BuildMetricsLog(request_metrics, stage_metrics));
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
                                            const ImageServiceStageMetrics& stage_metrics) const 
    {
        return "backend=" + std::string(use_depth_server_ ? "depth_anything_v2" : "pseudo_depth") +
               " decode_us=" + std::to_string(request_metrics.decode_us) +
               " depth_us=" + std::to_string(stage_metrics.depth_us) +
               " encode_us=" + std::to_string(request_metrics.encode_us) +
               " total_us=" + std::to_string(request_metrics.total_us);
    }
} // namespace ris
