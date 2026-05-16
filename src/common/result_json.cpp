#include "realtime_image_service/result_json.hpp"

#include <sstream>

namespace ris
{
    std::string BuildImageServiceMetricsJson(const ImageServiceMetrics& metrics)
    {
        std::ostringstream oss;

        oss << "{";
        oss << "\"backend\":\"" << metrics.backend << "\",";
        oss << "\"decode_us\":" << metrics.decode_us << ",";
        oss << "\"depth_us\":" << metrics.depth_us << ",";
        oss << "\"encode_us\":" << metrics.encode_us << ",";
        oss << "\"total_us\":" << metrics.total_us;
        oss << "}";

        return oss.str();
    }

    std::string BuildImageServiceResultJson(int input_width,
                                            int input_height,
                                            bool has_depth_image,
                                            const ImageServiceMetrics& metrics)
    {
        std::ostringstream oss;

        oss << "{";
        oss << "\"pipeline\":\"monocular_depth_visualization\",";
        oss << "\"input\":{";
        oss << "\"width\":" << input_width << ",";
        oss << "\"height\":" << input_height;
        oss << "},";
        oss << "\"outputs\":{";
        oss << "\"processed_image\":true,";
        oss << "\"depth_image\":" << (has_depth_image ? "true" : "false");
        oss << "},";
        oss << "\"metrics\":" << BuildImageServiceMetricsJson(metrics);
        oss << "}";

        return oss.str();
    }
}
