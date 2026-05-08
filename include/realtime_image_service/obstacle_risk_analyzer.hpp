#pragma once

#include <opencv2/opencv.hpp>
#include <string>

namespace ris
{
    struct ObstacleRiskConfig
    {
        float roi_x_ratio{0.0f};
        float roi_y_ratio{0.50f};
        float roi_width_ratio{1.0f};
        float roi_height_ratio{0.50f};

        float min_valid_depth{1e-6f};
        float max_valid_depth{1.0f};
        float near_depth_threshold{0.30f};
        float stop_depth_threshold{0.22f};
        float slow_depth_threshold{0.35f};
        float min_valid_ratio{0.20f};
        float high_min_obstacle_area_ratio{0.02f};
        float medium_min_obstacle_area_ratio{0.01f};
        float min_component_area_ratio{0.002f};
        int morph_kernel_size{3};
    };

    bool ValidateObstacleRiskConfig(const ObstacleRiskConfig& config,
                                    std::string* error_message = nullptr);

    // 障碍物风险分析结果
    struct ObstacleResult
    {
        // 是否检测到障碍物
        bool obstacle{false};

        // 风险等级：low / medium / high / unknown
        std::string risk_level{"low"};

        // 建议动作：keep / slow_down / stop / unknown
        std::string suggest_action{"keep"};

        // 置信度，范围 0.0 ~ 1.0
        float confidence{0.0f};

        // ROI 中近距离障碍物像素比例
        float near_ratio{0.0f};

        // ROI 中有效深度像素比例
        float valid_ratio{0.0f};

        // 障碍物连通区域面积占 ROI 的比例
        float obstacle_area_ratio{0.0f};

        // 鲁棒近距离深度，使用 5% 分位数，而不是直接用最小值
        float d05_depth{0.0f};

        // ROI 区域参数
        int roi_x{0};
        int roi_y{0};
        int roi_w{0};
        int roi_h{0};
    };

    class ObstacleRiskAnalyzer
    {
    public:
        ObstacleRiskAnalyzer() = default;
        explicit ObstacleRiskAnalyzer(ObstacleRiskConfig config);
        ~ObstacleRiskAnalyzer() = default;

        const ObstacleRiskConfig& config() const;

        // 分析归一化深度图中的障碍物风险
        //
        // depth_norm:
        //   支持 CV_32FC1 或 CV_8UC1
        //   如果是 CV_32FC1，默认数值范围是 0.0 ~ 1.0
        //   如果是 CV_8UC1，会自动转换到 0.0 ~ 1.0
        //
        // 注意：
        //   这里默认数值越小表示距离越近
        //   例如 value < 0.30f 表示近距离区域
        bool Analyze(const cv::Mat& depth_norm,
                     ObstacleResult* result,
                     std::string* error_message = nullptr) const;

        // 把分析结果画到图像上
        void DrawResult(const ObstacleResult& result,
                        cv::Mat* image) const;

    private:
        ObstacleRiskConfig config_;
    };
}
