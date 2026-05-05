#pragma once
// 这个模块把 JSON 和 JPEG 合成一个 payload。
// 负责打包和解包
#include <cstdint>
#include <string>
#include <vector>

namespace ris
{
    std::vector<uint8_t> PackResultPayload(const std::string& json,
                                           const std::vector<uint8_t>& image_bytes);

    bool UnpackResultPayload(const std::vector<uint8_t>& payload,
                             std::string* json,
                             std::vector<uint8_t>* image_bytes,
                             std::string* error_message);
}
