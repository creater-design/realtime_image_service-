#pragma once

#include <iostream>
#include <cstdint>
#include <vector>
#include <optional>

namespace ris
{
    // 1. 协议基础常量
    // constexpr 表示编译时常量，uint32_t 是无符号32位整数类型
    constexpr uint32_t kProtocolMagic = 0x52495345; // "RISE" in ASCII
    constexpr uint32_t kProtocolVersion = 1;

    // 头部严格固定为 16 字节
    constexpr size_t kHeaderSize = 16;
    // 数据包最大长度限制为 20MB 防止客户端发错数据导致服务端一直疯狂分配内存直到崩溃
    constexpr size_t kMaxPayloadSize = 20 * 1024 * 1024;

    // 2. 消息类型枚举
    enum class MessageType // 强类型枚举
    : uint16_t             // 底层类型说明
    {
        kImageRequest  = 1,   // 客户端发图片来请求处理
        kImageResponse = 2,  // 服务端处理完的图片返回
        kErrorResponse = 3,  // 服务端告诉客户端：出错了
    };

    // 3. 协议头部结构体 - 固定16字节
    struct MessageHeader 
    {
        uint32_t magic = kProtocolMagic;     // 4 字节：魔法数字
        uint16_t version = kProtocolVersion; // 2 字节：版本号
        uint16_t msg_type = 0;               // 2 字节：消息类型
        uint32_t payload_size = 0;           // 4 字节：后续图片数据的大小
        uint32_t request_id = 0;             // 4 字节：请求流水号（把请求和响应对应起来）
    };

    // 4. 完整数据包结构体 = 头部 + 可变长度数据
    struct Packet 
    {
        MessageHeader header;
        std::vector<uint8_t> payload;
    };

    // 函数声明
    // 把结构体变成要发送的字节流
    std::vector<uint8_t> SerializeHeader(const MessageHeader& header);

    // 从收到的字节流中还原出结构体 (如果数据不够或不合法，返回 std::nullopt)
    std::optional<MessageHeader> DeserializeHeader(const uint8_t* data, std::size_t len);

    // 方便在业务代码里一键打包的辅助工具
    std::vector<uint8_t> BuildPacket(MessageType msg_type,
                                    uint32_t request_id,
                                    const std::vector<uint8_t>& payload);

    std::vector<uint8_t> BuildErrorPacket(uint32_t request_id, const std::string& error_message);

} // namespace ris