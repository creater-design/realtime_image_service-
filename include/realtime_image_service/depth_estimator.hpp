#pragma once

#include <cstdint>
#include <string>

#include <opencv2/opencv.hpp>

namespace ris
{
    class DepthEstimator
    {
    public:
        DepthEstimator(std::string host, uint16_t port, int timeout_ms = 30000);

        bool EstimateDepth(const cv::Mat& input_bgr,
                           cv::Mat* depth_norm,
                           std::string* error_message) const;

    private:
        bool SendAll(int fd,
                     const uint8_t* data,
                     std::size_t size,
                     std::string* error_message) const;

        bool RecvExact(int fd,
                       uint8_t* data,
                       std::size_t size,
                       std::string* error_message) const;

        bool WaitFdReady(int fd,
                         short events,
                         std::string* error_message) const;

        std::string host_;
        uint16_t port_{18080};
        int timeout_ms_{30000};
    };
}
