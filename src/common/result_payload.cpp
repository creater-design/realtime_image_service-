#include "realtime_image_service/result_payload.hpp"

#include <arpa/inet.h> 
#include <cstring>

namespace ris
{
    namespace
    {
        // 该函数将32位无符号整数转换为网络字节序，随后将其4个字节追加到缓冲区末尾。通过htonl确保大端序兼容，
        // 再利用insert将二进制数据写入vector，常用于网络协议或序列化场景中的数据打包。
        void AppendUint32(std::vector<uint8_t>* buffer, uint32_t value)
        {
            const uint32_t net_value = htonl(value);
            const uint8_t* data = reinterpret_cast<const uint8_t*>(&net_value);
            buffer->insert(buffer->end(), data, data + sizeof(net_value));
        }

        bool ReadUint32(const std::vector<uint8_t>& buffer,
                        std::size_t offset,
                        uint32_t* value)
        {
            if (!value)
            {
                return false;
            }

            if (offset + sizeof(uint32_t) > buffer.size())
            {
                return false;
            }

            uint32_t net_value = 0;
            // buffer.data 返回指向容器数据最开始的内存指针
            std::memcpy(&net_value, buffer.data() + offset, sizeof(net_value));
            *value = ntohl(net_value);
            return true;
        }
    }

    std::vector<uint8_t> PackResultPayload(const std::string& json,
                                           const std::vector<uint8_t>& image_bytes)
    {
        std::vector<uint8_t> payload;
        
        // 
        payload.reserve(sizeof(uint32_t) + json.size() +
                        sizeof(uint32_t) + image_bytes.size());

        AppendUint32(&payload, static_cast<uint32_t>(json.size()));
        payload.insert(payload.end(), json.begin(), json.end());

        AppendUint32(&payload, static_cast<uint32_t>(image_bytes.size()));
        payload.insert(payload.end(), image_bytes.begin(), image_bytes.end());

        return payload;
    }

    bool UnpackResultPayload(const std::vector<uint8_t>& payload,
                             std::string* json,
                             std::vector<uint8_t>* image_bytes,
                             std::string* error_message)
    {
        if (!json || !image_bytes)
        {
            if (error_message) *error_message = "json or image_bytes pointer is null";
            return false;
        }

        json->clear();
        image_bytes->clear();

        std::size_t offset = 0;
        
        // payload 里存的是大端，网络字节序
        // 程序变量 json_size 里拿到的是正常整数-小端存储，主机字节序，ReadUint32最后做了转换。
        uint32_t json_size = 0;
        if (!ReadUint32(payload, offset, &json_size))
        {
            if (error_message) *error_message = "failed to read json_size";
            return false;
        }
        offset += sizeof(uint32_t);

        if (offset + json_size > payload.size())
        {
            if (error_message) *error_message = "invalid json_size";
            return false;
        }

        json->assign(reinterpret_cast<const char*>(payload.data() + offset), json_size);
        offset += json_size;

        uint32_t image_size = 0;
        if (!ReadUint32(payload, offset, &image_size))
        {
            if (error_message) *error_message = "failed to read image_size";
            return false;
        }
        offset += sizeof(uint32_t);

        if (offset + image_size > payload.size())
        {
            if (error_message) *error_message = "invalid image_size";
            return false;
        }

        image_bytes->assign(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                            payload.begin() + static_cast<std::ptrdiff_t>(offset + image_size));

        offset += image_size;

        if (offset != payload.size())
        {
            if (error_message) *error_message = "trailing bytes in result payload";
            return false;
        }

        return true;
    }
}
