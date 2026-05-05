#include "realtime_image_service/result_json.hpp"

#include <iomanip>
#include <sstream>

namespace ris
{
    std::string BuildObstacleResultJson(const ObstacleResult& result)
    {
        std::ostringstream oss;

        oss << "{";
        oss << "\"obstacle\":" << (result.obstacle ? "true" : "false") << ",";
        oss << "\"region\":\"front_center\",";
        oss << "\"risk_level\":\"" << result.risk_level << "\",";
        oss << "\"suggest_action\":\"" << result.suggest_action << "\",";
        oss << "\"near_ratio\":" << std::fixed << std::setprecision(4) << result.near_ratio << ",";
        oss << "\"confidence\":" << std::fixed << std::setprecision(4) << result.confidence << ",";
        oss << "\"valid_ratio\":" << std::fixed << std::setprecision(4) << result.valid_ratio << ",";
        oss << "\"obstacle_area_ratio\":" << std::fixed << std::setprecision(4) << result.obstacle_area_ratio << ",";
        oss << "\"d05_depth\":" << std::fixed << std::setprecision(4) << result.d05_depth << ",";
        oss << "\"roi\":{";
        oss << "\"x\":" << result.roi_x << ",";
        oss << "\"y\":" << result.roi_y << ",";
        oss << "\"w\":" << result.roi_w << ",";
        oss << "\"h\":" << result.roi_h;
        oss << "}";
        oss << "}";

        return oss.str();
    }
}
