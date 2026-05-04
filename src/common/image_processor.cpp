#include <opencv2/imgproc.hpp>
#include "realtime_image_service/image_processor.hpp"
#include "realtime_image_service/cuda_kernels.hpp"
#include "realtime_image_service/logger.hpp"

namespace ris
{

// 表示内部函数只在当前cpp文件中使用，避免命名冲突
namespace
{
    // 将输入图像转换为灰度图像
    bool ConvertToGray(const cv::Mat& input, cv::Mat* gray, std::string* error_message)
    {
        // 如果调用者连输出参数gray都没传，则返回错误
        if (!gray)
        {
            if (error_message)
            {
                *error_message = "Invalid gray image pointer";
            }
            return false;
        }

        // 检查输入图像是否为空
        if (input.empty())
        {
            if (error_message)
            {
                *error_message = "Input image is empty";
            }
            return false;
        }

        // 如果输入图像已经是灰度图像，则直接复制
        if (input.channels() == 1)
        {
            *gray = input.clone();
            return true;
        }

        // 将输入图像转换为灰度图像
        cv::cvtColor(input, *gray, cv::COLOR_BGR2GRAY);
        return true;
    }

}

    bool ProcessSobelCpu(const cv::Mat& input, cv::Mat* output, std::string* error_message)
    {
        return ProcessSobelCpuWithMetrics(input, output, nullptr, error_message);
    }

    bool ProcessSobelCuda(const cv::Mat& input, cv::Mat* output, std::string* error_message)
    {
        return ProcessSobelCudaWithMetrics(input, output, nullptr, error_message);
    }

    bool ProcessSobelCpuWithMetrics(const cv::Mat& input,
                                        cv::Mat* output,
                                        ProcessMetrics* metrics,
                                        std::string* error_message)
    { 
        if (!output)
        {
            if (error_message)
            {
                *error_message = "Invalid output image pointer";
            }
            return false;
        }

        const int64_t total_begin = NowUs();
        const int64_t preprocess_begin = NowUs();

        cv::Mat gray;
        if (!ConvertToGray(input, &gray, error_message))
        {
            return false;
        }

        const int64_t preprocess_end = NowUs();

        cv::Mat grad_x;
        cv::Mat grad_y;
        cv::Mat abs_grad_x;
        cv::Mat abs_grad_y;

        const int64_t compute_begin = NowUs();
        // 对灰度图 gray 做 Sobel 边缘检测，计算 x 方向的一阶导数，结果存到 grad_x 里，卷积核大小是 3
        // cv::Sobel(输入图, 输出图, 输出深度, x方向求导阶数, y方向求导阶数, 核大小)
        cv::Sobel(gray, grad_x, CV_16S, 1, 0, 3);
        cv::Sobel(gray, grad_y, CV_16S, 0, 1, 3);
        // 把 Sobel 计算出来的 x、y 方向梯度，变成能显示的图，再合成为最终边缘图
        cv::convertScaleAbs(grad_x, abs_grad_x);
        cv::convertScaleAbs(grad_y, abs_grad_y);
        // 合并梯度图，alpha 和 beta 是权重，gamma 是偏置值，会被加到最终结果上
        cv::addWeighted(abs_grad_x, 0.5, abs_grad_y, 0.5, 0, *output);
        const int64_t compute_end = NowUs();

        if (metrics)
        {
            metrics->preprocess_us = preprocess_end - preprocess_begin; 
            metrics->compute_us = compute_end - compute_begin;
            metrics->total_us = compute_end - total_begin;
        }
        return true;
    } // end of ProcessSobelCpuWithMetrics

    bool ProcessSobelCudaWithMetrics(const cv::Mat& input,
                                 cv::Mat* output,
                                 ProcessMetrics* metrics,
                                 std::string* error_message) 
    {
        #if !RIS_ENABLE_CUDA
        return ProcessSobelCpuWithMetrics(input, output, metrics, error_message);
        #else
        if (!output) 
        {
            if (error_message) {
            *error_message = "output image is null";
            }
            return false;
        }

        const int64_t total_begin = NowUs();
        const int64_t preprocess_begin = NowUs();
        // 将输入图像转换为灰度图像
        cv::Mat gray;
        if (!ConvertToGray(input, &gray, error_message)) 
        {
            return false;
        }
        const int64_t preprocess_end = NowUs();
        // 检查图像尺寸，Sobel算子需要至少3x3的图像
        if (gray.cols < 3 || gray.rows < 3) 
        {
            if (error_message) 
            {
                *error_message = "image is too small for sobel";
            }
            return false;
        }
        // 创建一个指定尺寸 + 指定类型的矩阵，并把所有像素值初始化为 0，作为 Sobel 边缘检测的输出图像
        *output = cv::Mat::zeros(gray.size(), CV_8UC1);
        ProcessMetrics local_metrics;
        // 调用 CUDA 内核函数执行 Sobel 边缘检测，传入输入图像数据、输出图像数据、以及用于记录性能指标的结构体
        if (!RunSobelCudaKernel(gray.data,
                                gray.cols,
                                gray.rows,
                                static_cast<int>(gray.step),
                                output->data,
                                static_cast<int>(output->step),
                                &local_metrics,
                                error_message)) 
        {
            return false;
        }

        const int64_t total_end = NowUs();
        local_metrics.preprocess_us = preprocess_end - preprocess_begin;
        local_metrics.total_us = total_end - total_begin;
        if (metrics) 
        {
            *metrics = local_metrics;
        }
        return true;
        #endif
    } // end of ProcessSobelCudaWithMetrics

} // namespace ris
