#pragma once

#include <string>

#include "realtime_image_service/obstacle_risk_analyzer.hpp"

namespace ris
{
    std::string BuildObstacleResultJson(const ObstacleResult& result);
}
