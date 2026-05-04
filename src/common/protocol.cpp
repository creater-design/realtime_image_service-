#include "realtime_image_service/protocol.hpp"
#include <arpa/inet.h> // 引入网络字节序转换函数 htons, htonl 等
#include <cstring>     // 引入 std::memcpy

// htons   // host → network（16位）
// htonl   // host → network（32位）
// ntohs   // network → host（16位）
// ntohl   // network → host（32位）

namespace ris
{

// 匿名命名空间：这里的函数只在当前 .cpp 文件内可见，是对外的隐藏逻辑
namespace
{
    // 实现接口的工具函数
    // 安全地写入 16 位整数（处理了大小端和内存对齐）
    void WriteUint16(uint8_t* dst, uint16_t value) 
    {
        // 把一个 uint16_t 值转换成网络字节序
        // 把这 2 个字节写到 data 指向的内存里
        uint16_t net_value = htons(value);
        std::memcpy(dst, &net_value, sizeof(net_value));
    }

    // 安全地写入 32 位整数（处理了大小端和内存对齐）
    void WriteUint32(uint8_t* dst, uint32_t value)
    {
        uint32_t net_value = htonl(value);
        std::memcpy(dst, &net_value, sizeof(net_value));
    }

    // 从网络字节序的内存里安全地读取 16 位整数
    uint16_t ReadUint16(const uint8_t* src)
    {
        uint16_t net_value;
        std::memcpy(&net_value, src, sizeof(net_value));
        return ntohs(net_value);
    }

    // 从网络字节序的内存里安全地读取 32 位整数
    uint32_t ReadUint32(const uint8_t* src)
    {
        uint32_t net_value;
        std::memcpy(&net_value, src, sizeof(net_value));
        return ntohl(net_value);
    }

} // namespace

    // 把结构体变成要发送的字节流发往网络
    std::vector<uint8_t> SerializeHeader(const MessageHeader& header)
    {
        std::vector<uint8_t> buffer(kHeaderSize, 0); 
        // buffer.data() 是 C++ 容器提供的底层内存指针接口，本质上就是：
        // 返回指向连续内存首地址的指针
        WriteUint32(buffer.data(), header.magic);
        WriteUint16(buffer.data() + 4, header.version);
        WriteUint16(buffer.data() + 6, header.msg_type);
        WriteUint32(buffer.data() + 8, header.payload_size);
        WriteUint32(buffer.data() + 12, header.request_id); 
        return buffer;
    }

    // 从网络收到字节流后，还原成结构体
    std::optional<MessageHeader> DeserializeHeader(const uint8_t* data, std::size_t len)
    {
        if (len < kHeaderSize) 
        {
            // 数据不够，无法还原出完整的头部
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
            // 数据不合法，魔法数字或版本号不对，或者声明的 payload 太大了
            return std::nullopt;
        }

        return header;
    }

    // 业务代码里一键打包的辅助工具
    std::vector<uint8_t> BuildPacket(MessageType msg_type,
                                    uint32_t request_id,
                                    const std::vector<uint8_t>& payload)
    {
        // 底层类型只决定存储大小，不提供隐式转换。
        // enum class 是强类型枚举，必须显式转换才能进入协议层，这样可以保证类型安全和协议边界清晰。
        MessageHeader header;
        header.msg_type = static_cast<uint16_t>(msg_type);
        header.request_id = request_id;
        header.payload_size = static_cast<uint32_t>(payload.size()); // size_t 是平台相关的无符号整数类型

        // 先序列化头部，再把 payload 拼接到后面
        std::vector<uint8_t> packet = SerializeHeader(header);
        // 直接把 payload 的内容追加到 packet 的末尾
        packet.insert(packet.end(), payload.begin(), payload.end());
        return packet;
    }

    // 服务端报错时快速返回的包
    std::vector<uint8_t> BuildErrorPacket(uint32_t request_id, const std::string& error_message)
    {
        // string 是字符序列，vector<uint8_t> 是字节序列，我们需要把字符串转换成字节流才能放到协议里发送
        std::vector<uint8_t> payload(error_message.begin(), error_message.end());
        return BuildPacket(MessageType::kErrorResponse, request_id, payload);
    }
} // namespace ris




