#pragma once

#include <cstdint>
#include <string>

#include "realtime_image_service/obstacle_risk_analyzer.hpp"

namespace ris
{
    struct ImageServiceMetrics
    {
        std::string backend{"cpu"};
        int64_t decode_us{0};
        int64_t preprocess_us{0};
        int64_t risk_us{0};
        int64_t encode_us{0};
        int64_t total_us{0};
    };

    std::string BuildObstacleResultJson(const ObstacleResult& result);
    std::string BuildObstacleResultJson(const ObstacleResult& result,
                                        const ImageServiceMetrics& metrics);
    std::string BuildImageServiceMetricsJson(const ImageServiceMetrics& metrics);
}
