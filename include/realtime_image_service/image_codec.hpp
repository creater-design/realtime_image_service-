#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

namespace ris
{
    bool EncodeJpeg(const cv::Mat& image,
                std::vector<uint8_t>* output,
                int quality,
                std::string* error_message);
                
    bool DecodeImage(const std::vector<uint8_t>& input,
                    cv::Mat* image,
                    std::string* error_message);
}

