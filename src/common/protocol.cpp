#include "realtime_image_service/protocol.hpp"
#include <arpa/inet.h>
#include <cstring>

// Protocol header layout, all integer fields are network byte order:
// magic(4) + version(2) + msg_type(2) + payload_size(4) + request_id(4).

namespace ris
{

namespace
{
    void WriteUint16(uint8_t* dst, uint16_t value) 
    {
        uint16_t net_value = htons(value);
        std::memcpy(dst, &net_value, sizeof(net_value));
    }

    void WriteUint32(uint8_t* dst, uint32_t value)
    {
        uint32_t net_value = htonl(value);
        std::memcpy(dst, &net_value, sizeof(net_value));
    }

    uint16_t ReadUint16(const uint8_t* src)
    {
        uint16_t net_value;
        std::memcpy(&net_value, src, sizeof(net_value));
        return ntohs(net_value);
    }

    uint32_t ReadUint32(const uint8_t* src)
    {
        uint32_t net_value;
        std::memcpy(&net_value, src, sizeof(net_value));
        return ntohl(net_value);
    }

} // namespace

    std::vector<uint8_t> SerializeHeader(const MessageHeader& header)
    {
        std::vector<uint8_t> buffer(kHeaderSize, 0); 
        WriteUint32(buffer.data(), header.magic);
        WriteUint16(buffer.data() + 4, header.version);
        WriteUint16(buffer.data() + 6, header.msg_type);
        WriteUint32(buffer.data() + 8, header.payload_size);
        WriteUint32(buffer.data() + 12, header.request_id); 
        return buffer;
    }

    std::optional<MessageHeader> DeserializeHeader(const uint8_t* data, std::size_t len)
    {
        if (len < kHeaderSize) 
        {
            return std::nullopt;
        }

        MessageHeader header;
        header.magic = ReadUint32(data);
        header.version = ReadUint16(data + 4);
        header.msg_type = ReadUint16(data + 6);
        header.payload_size = ReadUint32(data + 8);
        header.request_id = ReadUint32(data + 12);

        if (header.magic != kProtocolMagic || header.version != kProtocolVersion || header.payload_size > kMaxPayloadSize) 
        {
            return std::nullopt;
        }

        return header;
    }

    std::vector<uint8_t> BuildPacket(MessageType msg_type,
                                    uint32_t request_id,
                                    const std::vector<uint8_t>& payload)
    {
        MessageHeader header;
        header.msg_type = static_cast<uint16_t>(msg_type);
        header.request_id = request_id;
        header.payload_size = static_cast<uint32_t>(payload.size());

        // Header + payload gives Muduo/TCP readers enough information to split the stream.
        std::vector<uint8_t> packet = SerializeHeader(header);
        packet.insert(packet.end(), payload.begin(), payload.end());
        return packet;
    }

    std::vector<uint8_t> BuildErrorPacket(uint32_t request_id, const std::string& error_message)
    {
        std::vector<uint8_t> payload(error_message.begin(), error_message.end());
        return BuildPacket(MessageType::kErrorResponse, request_id, payload);
    }
} // namespace ris


