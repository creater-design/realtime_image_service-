#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

namespace ris
{
    class DepthEstimator
    {
    public:
        DepthEstimator(std::string host, uint16_t port, int timeout_ms = 30000);
        ~DepthEstimator();

        bool EstimateDepth(const cv::Mat& input_bgr,
                           cv::Mat* depth_norm,
                           std::string* error_message);

        void Close();

    private:
        bool EnsureConnectedLocked(std::string* error_message);
        bool ConnectLocked(std::string* error_message);
        void CloseLocked();

        bool ExchangeLocked(const std::vector<uint8_t>& encoded_image,
                            uint32_t* status,
                            std::vector<uint8_t>* payload,
                            std::string* error_message);

        bool SendAllLocked(const uint8_t* data,
                     std::size_t size,
                     std::string* error_message);

        bool RecvExactLocked(uint8_t* data,
                       std::size_t size,
                       std::string* error_message);

        bool WaitFdReadyLocked(short events,
                               std::string* error_message) const;

        std::string host_;
        uint16_t port_{18080};
        int timeout_ms_{30000};
        int sockfd_{-1};
        std::mutex mutex_;
    };
}
