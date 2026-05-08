#include "realtime_image_service/result_payload.hpp"

#include <arpa/inet.h> 
#include <cstring>

namespace ris
{
    namespace
    {
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
            std::memcpy(&net_value, buffer.data() + offset, sizeof(net_value));
            *value = ntohl(net_value);
            return true;
        }
    }

    std::vector<uint8_t> PackResultPayload(const std::string& json,
                                           const std::vector<uint8_t>& image_bytes)
    {
        return PackResultPayloadWithDepth(json, image_bytes, {});
    }

    std::vector<uint8_t> PackResultPayloadWithDepth(const std::string& json,
                                                    const std::vector<uint8_t>& image_bytes,
                                                    const std::vector<uint8_t>& depth_image_bytes)
    {
        // Response payload layout:
        // json_size + json + image_size + processed_jpeg + optional depth_size + depth_jpeg.
        std::vector<uint8_t> payload;
        
        payload.reserve(sizeof(uint32_t) + json.size() +
                        sizeof(uint32_t) + image_bytes.size() +
                        sizeof(uint32_t) + depth_image_bytes.size());

        AppendUint32(&payload, static_cast<uint32_t>(json.size()));
        payload.insert(payload.end(), json.begin(), json.end());

        AppendUint32(&payload, static_cast<uint32_t>(image_bytes.size()));
        payload.insert(payload.end(), image_bytes.begin(), image_bytes.end());

        if (!depth_image_bytes.empty())
        {
            AppendUint32(&payload, static_cast<uint32_t>(depth_image_bytes.size()));
            payload.insert(payload.end(), depth_image_bytes.begin(), depth_image_bytes.end());
        }

        return payload;
    }

    bool UnpackResultPayload(const std::vector<uint8_t>& payload,
                             std::string* json,
                             std::vector<uint8_t>* image_bytes,
                             std::string* error_message)
    {
        std::vector<uint8_t> ignored_depth_image;
        return UnpackResultPayloadWithDepth(
            payload,
            json,
            image_bytes,
            &ignored_depth_image,
            error_message
        );
    }

    bool UnpackResultPayloadWithDepth(const std::vector<uint8_t>& payload,
                                      std::string* json,
                                      std::vector<uint8_t>* image_bytes,
                                      std::vector<uint8_t>* depth_image_bytes,
                                      std::string* error_message)
    {
        if (!json || !image_bytes || !depth_image_bytes)
        {
            if (error_message) *error_message = "json or image pointer is null";
            return false;
        }

        json->clear();
        image_bytes->clear();
        depth_image_bytes->clear();

        std::size_t offset = 0;
        
        // Each variable-length field is length-prefixed so binary JPEG bytes can be
        // packed together with UTF-8 JSON without separator escaping.
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

        if (offset == payload.size())
        {
            return true;
        }

        uint32_t depth_image_size = 0;
        if (!ReadUint32(payload, offset, &depth_image_size))
        {
            if (error_message) *error_message = "failed to read depth_image_size";
            return false;
        }
        offset += sizeof(uint32_t);

        if (offset + depth_image_size > payload.size())
        {
            if (error_message) *error_message = "invalid depth_image_size";
            return false;
        }

        depth_image_bytes->assign(
            payload.begin() + static_cast<std::ptrdiff_t>(offset),
            payload.begin() + static_cast<std::ptrdiff_t>(offset + depth_image_size)
        );

        offset += depth_image_size;

        if (offset != payload.size())
        {
            if (error_message) *error_message = "trailing bytes in result payload";
            return false;
        }

        return true;
    }
}
