#include "realtime_image_service/obstacle_risk_analyzer.hpp"

#include <algorithm>   // std::clamp, std::max, std::nth_element
#include <cmath>       // std::isfinite
#include <iomanip>     // std::fixed, std::setprecision
#include <limits>      // std::numeric_limits
#include <sstream>     // std::ostringstream
#include <vector>      // std::vector

namespace ris
{
    namespace
    {
        // 计算 5% 分位数
        // 作用：
        //   不直接取最小深度，避免一个噪声点导致误判
        //   取较近的一小部分深度中的代表值，更稳定
        float Percentile05(std::vector<float>* values)
        {
            if (!values || values->empty())
            {
                return 0.0f;
            }

            const std::size_t n = values->size();

            // 5% 分位点下标
            std::size_t index = static_cast<std::size_t>(static_cast<float>(n) * 0.05f);

            if (index >= n)
            {
                index = n - 1;
            }

            // nth_element 不会完全排序整个数组
            // 它只保证 values[index] 放到了排序后应该在的位置
            // 比完整 sort 更快
            std::nth_element(values->begin(),
                             values->begin() + static_cast<std::ptrdiff_t>(index),
                             values->end());

            return (*values)[index];
        }
    }

    bool ValidateObstacleRiskConfig(const ObstacleRiskConfig& config,
                                    std::string* error_message)
    {
        const auto fail = [error_message](const std::string& message) {
            if (error_message)
            {
                *error_message = message;
            }
            return false;
        };

        if (config.roi_x_ratio < 0.0f || config.roi_x_ratio >= 1.0f)
        {
            return fail("roi_x_ratio must be in [0.0, 1.0)");
        }
        if (config.roi_y_ratio < 0.0f || config.roi_y_ratio >= 1.0f)
        {
            return fail("roi_y_ratio must be in [0.0, 1.0)");
        }
        if (config.roi_width_ratio <= 0.0f || config.roi_width_ratio > 1.0f)
        {
            return fail("roi_width_ratio must be in (0.0, 1.0]");
        }
        if (config.roi_height_ratio <= 0.0f || config.roi_height_ratio > 1.0f)
        {
            return fail("roi_height_ratio must be in (0.0, 1.0]");
        }
        if (config.roi_x_ratio + config.roi_width_ratio > 1.0f)
        {
            return fail("roi_x_ratio + roi_width_ratio must be <= 1.0");
        }
        if (config.roi_y_ratio + config.roi_height_ratio > 1.0f)
        {
            return fail("roi_y_ratio + roi_height_ratio must be <= 1.0");
        }
        if (config.min_valid_depth < 0.0f ||
            config.max_valid_depth <= config.min_valid_depth ||
            config.max_valid_depth > 1.0f)
        {
            return fail("depth valid range must satisfy 0 <= min_valid_depth < max_valid_depth <= 1");
        }
        if (config.near_depth_threshold <= config.min_valid_depth ||
            config.near_depth_threshold >= config.max_valid_depth)
        {
            return fail("near_depth_threshold must be inside the valid depth range");
        }
        if (config.stop_depth_threshold <= config.min_valid_depth ||
            config.stop_depth_threshold >= config.max_valid_depth ||
            config.slow_depth_threshold <= config.stop_depth_threshold ||
            config.slow_depth_threshold >= config.max_valid_depth)
        {
            return fail("depth thresholds must satisfy min < stop < slow < max");
        }
        if (config.min_valid_ratio < 0.0f || config.min_valid_ratio > 1.0f ||
            config.high_min_obstacle_area_ratio < 0.0f ||
            config.high_min_obstacle_area_ratio > 1.0f ||
            config.medium_min_obstacle_area_ratio < 0.0f ||
            config.medium_min_obstacle_area_ratio > 1.0f ||
            config.min_component_area_ratio < 0.0f ||
            config.min_component_area_ratio > 1.0f)
        {
            return fail("ratio thresholds must be in [0.0, 1.0]");
        }
        if (config.high_min_obstacle_area_ratio < config.medium_min_obstacle_area_ratio)
        {
            return fail("high_min_obstacle_area_ratio must be >= medium_min_obstacle_area_ratio");
        }
        if (config.morph_kernel_size <= 0 || config.morph_kernel_size % 2 == 0)
        {
            return fail("morph_kernel_size must be a positive odd integer");
        }

        return true;
    }

    ObstacleRiskAnalyzer::ObstacleRiskAnalyzer(ObstacleRiskConfig config)
    : config_(config)
    {
        std::string error_message;
        if (!ValidateObstacleRiskConfig(config_, &error_message))
        {
            config_ = ObstacleRiskConfig{};
        }
    }

    const ObstacleRiskConfig& ObstacleRiskAnalyzer::config() const
    {
        return config_;
    }

    bool ObstacleRiskAnalyzer::Analyze(const cv::Mat& depth_norm,
                                       ObstacleResult* result,
                                       std::string* error_message) const
    {
        // ================================
        // 1. 检查输出结果指针是否有效
        // ================================
        if (!result)
        {
            if (error_message)
            {
                *error_message = "ObstacleResult pointer is null";
            }
            return false;
        }

        // 每次分析前先重置 result，避免保留上一次结果
        *result = ObstacleResult{};

        // ================================
        // 2. 检查输入深度图是否为空
        // ================================
        if (depth_norm.empty())
        {
            if (error_message)
            {
                *error_message = "depth_norm is empty";
            }
            return false;
        }

        // ================================
        // 3. 把输入深度图统一转换成 CV_32FC1
        // ================================
        cv::Mat depth_float;

        if (depth_norm.type() == CV_32FC1)
        {
            // 如果本身就是 float 单通道，直接使用
            // 这里是浅拷贝，不会复制整张图像
            depth_float = depth_norm;
        }
        else if (depth_norm.type() == CV_8UC1)
        {
            // 如果是 8 位灰度图，则转换成 0.0 ~ 1.0 的 float 图
            depth_norm.convertTo(depth_float, CV_32FC1, 1.0 / 255.0);
        }
        else
        {
            if (error_message)
            {
                *error_message = "depth_norm must be CV_32FC1 or CV_8UC1";
            }
            return false;
        }

        const int width = depth_float.cols;
        const int height = depth_float.rows;

        if (width <= 0 || height <= 0)
        {
            if (error_message)
            {
                *error_message = "invalid depth image size";
            }
            return false;
        }

        // ROI is configured by ratio. The default covers the lower half of the frame.
        result->roi_x = static_cast<int>(static_cast<float>(width) * config_.roi_x_ratio);
        result->roi_y = static_cast<int>(static_cast<float>(height) * config_.roi_y_ratio);
        result->roi_w = static_cast<int>(static_cast<float>(width) * config_.roi_width_ratio);
        result->roi_h = static_cast<int>(static_cast<float>(height) * config_.roi_height_ratio);

        // 防止 ROI 越界
        result->roi_x = std::clamp(result->roi_x, 0, width - 1);
        result->roi_y = std::clamp(result->roi_y, 0, height - 1);
        result->roi_w = std::clamp(result->roi_w, 1, width - result->roi_x);
        result->roi_h = std::clamp(result->roi_h, 1, height - result->roi_y);

        const cv::Rect roi(result->roi_x,
                           result->roi_y,
                           result->roi_w,
                           result->roi_h);

        const cv::Mat roi_depth = depth_float(roi);

        const int roi_total_pixels = roi_depth.rows * roi_depth.cols;

        if (roi_total_pixels <= 0)
        {
            if (error_message)
            {
                *error_message = "invalid ROI size";
            }
            return false;
        }

        // ================================
        // 5. 构建近距离二值 mask
        // ================================
        // near_mask:
        //   0   表示不是近距离障碍物
        //   255 表示近距离候选障碍物
        cv::Mat near_mask = cv::Mat::zeros(roi_depth.size(), CV_8UC1);

        int valid_count = 0;
        int raw_near_count = 0;

        // 保存近距离点的深度值，用于后续计算 d05 分位深度
        std::vector<float> raw_near_depth_values;
        raw_near_depth_values.reserve(static_cast<std::size_t>(roi_total_pixels / 4));

        for (int y = 0; y < roi_depth.rows; ++y)
        {
            const float* depth_row = roi_depth.ptr<float>(y);
            uchar* mask_row = near_mask.ptr<uchar>(y);

            for (int x = 0; x < roi_depth.cols; ++x)
            {
                const float value = depth_row[x];

                // 过滤 NaN / Inf
                if (!std::isfinite(value))
                {
                    continue;
                }

                // 过滤无效深度
                if (value <= config_.min_valid_depth || value > config_.max_valid_depth)
                {
                    continue;
                }

                ++valid_count;

                // 小于近距离阈值，认为是近距离候选障碍点
                if (value < config_.near_depth_threshold)
                {
                    mask_row[x] = 255;
                    ++raw_near_count;
                    raw_near_depth_values.push_back(value);
                }
            }
        }

        // ================================
        // 6. 检查有效深度比例
        // ================================
        if (valid_count == 0)
        {
            if (error_message)
            {
                *error_message = "no valid depth value in ROI";
            }
            return false;
        }

        result->valid_ratio =
            static_cast<float>(valid_count) / static_cast<float>(roi_total_pixels);

        // 有效深度太少，判断为 unknown
        // 不建议在深度信息严重不足时强行输出 low
        if (result->valid_ratio < config_.min_valid_ratio)
        {
            result->obstacle = false;
            result->risk_level = "unknown";
            result->suggest_action = "unknown";
            result->near_ratio =
                static_cast<float>(raw_near_count) / static_cast<float>(valid_count);
            result->confidence = 0.0f;
            result->obstacle_area_ratio = 0.0f;
            result->d05_depth = 0.0f;

            if (error_message)
            {
                *error_message = "too few valid depth values in ROI";
            }

            return true;
        }

        // 原始 near_ratio
        // 这个值表示没有形态学和连通域过滤之前的近距离比例
        result->near_ratio =
            static_cast<float>(raw_near_count) / static_cast<float>(valid_count);

        // 如果一个近距离点都没有，直接判断低风险
        if (raw_near_count == 0)
        {
            result->obstacle = false;
            result->risk_level = "low";
            result->suggest_action = "keep";
            result->confidence = 0.0f;
            result->obstacle_area_ratio = 0.0f;
            result->d05_depth = 0.0f;
            return true;
        }

        // ================================
        // 7. 形态学去噪
        // ================================
        // 开运算：先腐蚀再膨胀，用于去掉小噪声点
        // 闭运算：先膨胀再腐蚀，用于连接小断裂区域
        const cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(config_.morph_kernel_size, config_.morph_kernel_size)
        );

        cv::Mat clean_mask;
        cv::morphologyEx(near_mask, clean_mask, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(clean_mask, clean_mask, cv::MORPH_CLOSE, kernel);

        // ================================
        // 8. 连通域分析
        // ================================
        // connectedComponentsWithStats 会把二值图中的连通区域找出来
        //
        // labels:
        //   每个像素属于哪个连通域
        //
        // stats:
        //   每个连通域的外接矩形、面积等信息
        //
        // centroids:
        //   每个连通域的中心点
        cv::Mat labels;
        cv::Mat stats;
        cv::Mat centroids;

        const int component_count = cv::connectedComponentsWithStats(
            clean_mask,
            labels,
            stats,
            centroids,
            8,
            CV_32S
        );

        const int min_component_area =
            std::max(10, static_cast<int>(static_cast<float>(roi_total_pixels) *
                                          config_.min_component_area_ratio));

        cv::Mat filtered_mask = cv::Mat::zeros(clean_mask.size(), CV_8UC1);

        int obstacle_area = 0;

        // 从 1 开始，因为 0 是背景
        for (int i = 1; i < component_count; ++i)
        {
            const int area = stats.at<int>(i, cv::CC_STAT_AREA);

            // 面积太小的连通域认为是噪声，不保留
            if (area < min_component_area)
            {
                continue;
            }

            obstacle_area += area;

            // 把这个连通域写入 filtered_mask
            filtered_mask.setTo(255, labels == i);
        }

        result->obstacle_area_ratio =
            static_cast<float>(obstacle_area) / static_cast<float>(roi_total_pixels);

        // 连通域过滤之后没有可靠障碍物
        if (obstacle_area == 0)
        {
            result->obstacle = false;
            result->risk_level = "low";
            result->suggest_action = "keep";
            result->confidence = 0.0f;
            result->d05_depth = 0.0f;
            return true;
        }

        // ================================
        // 9. 重新收集过滤后障碍物区域的深度值
        // ================================
        std::vector<float> obstacle_depth_values;
        obstacle_depth_values.reserve(static_cast<std::size_t>(obstacle_area));

        for (int y = 0; y < roi_depth.rows; ++y)
        {
            const float* depth_row = roi_depth.ptr<float>(y);
            const uchar* mask_row = filtered_mask.ptr<uchar>(y);

            for (int x = 0; x < roi_depth.cols; ++x)
            {
                if (mask_row[x] == 0)
                {
                    continue;
                }

                const float value = depth_row[x];

                if (!std::isfinite(value))
                {
                    continue;
                }

                if (value <= config_.min_valid_depth || value > config_.max_valid_depth)
                {
                    continue;
                }

                obstacle_depth_values.push_back(value);
            }
        }

        if (obstacle_depth_values.empty())
        {
            result->obstacle = false;
            result->risk_level = "low";
            result->suggest_action = "keep";
            result->confidence = 0.0f;
            result->d05_depth = 0.0f;
            return true;
        }

        // d05_depth 表示障碍物区域中比较近的一批点的代表距离
        // 比直接 min_depth 更抗噪声
        result->d05_depth = Percentile05(&obstacle_depth_values);

        // ================================
        // 10. 按企业工程常用的 stop / slow / keep 逻辑分级
        // ================================
        if (result->obstacle_area_ratio > config_.high_min_obstacle_area_ratio &&
            result->d05_depth < config_.stop_depth_threshold)
        {
            result->obstacle = true;
            result->risk_level = "high";
            result->suggest_action = "stop";
        }
        else if (result->obstacle_area_ratio > config_.medium_min_obstacle_area_ratio &&
                 result->d05_depth < config_.slow_depth_threshold)
        {
            result->obstacle = true;
            result->risk_level = "medium";
            result->suggest_action = "slow_down";
        }
        else
        {
            result->obstacle = false;
            result->risk_level = "low";
            result->suggest_action = "keep";
        }

        // ================================
        // 11. 计算 confidence
        // ================================
        // confidence 不是严格概率，而是工程风险评分。
        //
        // 这里综合三个因素：
        //   1. 距离风险：障碍物越近，风险越高
        //   2. 面积风险：障碍物区域越大，风险越高
        //   3. 有效性风险：深度越可靠，评分越可信
        //
        // 注意：
        //   归一化深度中，数值越小表示越近。
        const float distance_risk =
            std::clamp((config_.slow_depth_threshold - result->d05_depth) /
                       config_.slow_depth_threshold,
                       0.0f,
                       1.0f);

        const float area_risk =
            std::clamp(result->obstacle_area_ratio / 0.10f,
                       0.0f,
                       1.0f);

        const float valid_score =
            std::clamp(result->valid_ratio / 0.60f,
                       0.0f,
                       1.0f);

        result->confidence =
            0.55f * distance_risk +
            0.35f * area_risk +
            0.10f * valid_score;

        result->confidence = std::clamp(result->confidence, 0.0f, 1.0f);

        // 高风险时，工程上通常希望置信度不要太低
        if (result->risk_level == "high")
        {
            result->confidence = std::max(result->confidence, 0.85f);
        }
        else if (result->risk_level == "medium")
        {
            result->confidence = std::max(result->confidence, 0.55f);
        }

        return true;
    }

    void ObstacleRiskAnalyzer::DrawResult(const ObstacleResult& result,
                                          cv::Mat* image) const
    {
        // image 是指针，所以先判断是否为空
        if (!image || image->empty())
        {
            return;
        }

        // ================================
        // 1. 根据风险等级选择颜色
        // ================================
        // OpenCV 默认颜色顺序是 BGR，不是 RGB
        //
        // high    -> 红色
        // medium  -> 橙色
        // unknown -> 灰色
        // low     -> 绿色
        const cv::Scalar color =
            result.risk_level == "high" ? cv::Scalar(0, 0, 255) :
            result.risk_level == "medium" ? cv::Scalar(0, 165, 255) :
            result.risk_level == "unknown" ? cv::Scalar(128, 128, 128) :
            cv::Scalar(0, 255, 0);

        // ================================
        // 2. 绘制 ROI 矩形框
        // ================================
        cv::Rect roi(result.roi_x,
                     result.roi_y,
                     result.roi_w,
                     result.roi_h);

        cv::rectangle(*image, roi, color, 2);

        // ================================
        // 3. 拼接显示文本
        // ================================
        // 显示内容示例：
        //   risk=high action=stop conf=0.91 near=0.18 d05=0.16 area=0.05
        std::ostringstream oss;
        oss << "risk=" << result.risk_level
            << " action=" << result.suggest_action
            << " conf=" << std::fixed << std::setprecision(2) << result.confidence
            << " near=" << std::fixed << std::setprecision(2) << result.near_ratio
            << " d05=" << std::fixed << std::setprecision(2) << result.d05_depth
            << " area=" << std::fixed << std::setprecision(2) << result.obstacle_area_ratio;

        // ================================
        // 4. 设置文字位置
        // ================================
        // x 坐标和 ROI 左边界对齐
        // y 坐标尽量放在 ROI 上方 8 个像素
        // 但是最小不能小于 20，避免文字画到图像外面
        const cv::Point text_position(
            result.roi_x,
            std::max(20, result.roi_y - 8)
        );

        // ================================
        // 5. 绘制文字
        // ================================
        cv::putText(*image,
                    oss.str(),
                    text_position,
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.45,
                    color,
                    2);
    }
}
