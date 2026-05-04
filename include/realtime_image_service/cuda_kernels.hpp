#pragma once

#include <cstdint>
#include <string>
#include "realtime_image_service/image_processor.hpp"

namespace ris {

bool RunSobelCudaKernel(const uint8_t* input,
                        int width,
                        int height,
                        int input_step,
                        uint8_t* output,
                        int output_step,
                        ProcessMetrics* metrics,
                        std::string* error_message);

}  // namespace ris