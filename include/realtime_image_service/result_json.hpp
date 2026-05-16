#pragma once

#include <cstdint>
#include <string>

namespace ris
{
    struct ImageServiceMetrics
    {
        std::string backend{"cpu"};
        int64_t decode_us{0};
        int64_t depth_us{0};
        int64_t encode_us{0};
        int64_t total_us{0};
    };

    std::string BuildImageServiceResultJson(int input_width,
                                            int input_height,
                                            bool has_depth_image,
                                            const ImageServiceMetrics& metrics);
    std::string BuildImageServiceMetricsJson(const ImageServiceMetrics& metrics);
}
