#pragma once

#include <string>
#include <opencv2/opencv.hpp>

namespace ris
{
    // 记录性能指标的结构体
    struct ProcessMetrics
    {
        int64_t preprocess_us = 0; // 预处理时间（微秒）
        int64_t compute_us = 0; // 计算时间（微秒）
        int64_t transfer_h2d_us = 0; // 主机到设备的数据传输时间（微秒）
        int64_t transfer_d2h_us = 0; // 设备到主机的数据传输时间（微秒）
        int64_t total_us = 0; // 总时间（微秒）
    };

    // 用 CPU 对输入图像做 Sobel 边缘检测
    bool ProcessSobelCpu(const cv::Mat& input, cv::Mat* output, std::string* error_message);

    // 用 CUDA 对输入图像做 Sobel 边缘检测
    bool ProcessSobelCuda(const cv::Mat& input, cv::Mat* output, std::string* error_message);

    // 用 CPU 对输入图像做 Sobel 边缘检测，并记录性能指标
    bool ProcessSobelCpuWithMetrics(const cv::Mat& input,
                                    cv::Mat* output,
                                    ProcessMetrics* metrics,
                                    std::string* error_message);

    // 用 CUDA 对输入图像做 Sobel 边缘检测，并记录性能指标
    bool ProcessSobelCudaWithMetrics(const cv::Mat& input,
                                    cv::Mat* output,
                                    ProcessMetrics* metrics,
                                    std::string* error_message);
}
