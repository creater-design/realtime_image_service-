#include <cstring>
#include <cerrno>
#include <sys/socket.h> // 包含 socket 函数和相关结构体定义
#include <arpa/inet.h> // IP 地址转换（inet_pton、htons）
#include <unistd.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <algorithm>

#include "realtime_image_service/tcp_image_client.hpp"
#include "realtime_image_service/image_codec.hpp"
#include "realtime_image_service/protocol.hpp"
#include "realtime_image_service/result_payload.hpp"

namespace ris
{

namespace 
{
    // constexpr 是 C++11 引入的关键字，用于声明编译时常量。
    constexpr int kSendTimeoutMs = 10000; // 发送超时时间（毫秒）
    constexpr int kRecvTimeoutMs = 10000; // 接收超时时间（毫秒）
    constexpr std::size_t kSendChunkBytes = 64 * 1024; // 每次发送的最大字节数（64KB）

    // 等待 fd 处于就绪状态，可以进行读写操作
    bool WaitFdReady(int fd, short events, int timeout_ms, std::string* error_message) 
    {
        // 创建一个 pollfd 结构体 pfd
        // 让它监视 fd
        // 关注的事件类型是 events
        pollfd pfd {}; // pollfd 结构体，初始化为 0
        pfd.fd = fd;
        pfd.events = events;

        while (true) 
        {
            // ::poll 表示调用全局命名空间里的系统函数 poll，不是类成员函数，也不是别的同名函数
            // 监视 &pfd 这个数组
            // 数组里有 1 个 fd
            // 最多等待 timeout_ms 毫秒
            const int rc = ::poll(&pfd, 1, timeout_ms);
            // 有事件发生，或者发生错误，或者超时
            if (rc > 0) 
            {   
                // POLLERR：描述符出错
                // POLLHUP：对端挂断
                // POLLNVAL：fd 无效
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
                {
                    if (error_message) *error_message = "socket error/hup";
                    return false;
                }
                return true;
            }

            // rc == 0 → 超时 
            if (rc == 0) 
            {
                if (error_message) *error_message = "socket wait timeout";
                return false;
            }

            // rc < 0 且是 EINTR → 被信号中断，继续等待
            if (errno == EINTR) continue;

            // rc < 0 且不是 EINTR → 发生错误
            // 比如：
            // 参数不合法
            // fd 有问题
            // 系统调用失败
            if (error_message) *error_message = std::string("poll failed: ") + std::strerror(errno);
            return false;
        }
}

} // namespace
    TcpImageClient::TcpImageClient(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) {}

    TcpImageClient::~TcpImageClient() 
    {
        Close();
    }

    bool TcpImageClient::Connect(std::string* error_message) 
    {
        if (sockfd_ >= 0) 
        {
            return true;
        }
        // 创建 TCP 套接字 
        // | `AF_INET`     | IPv4   |
        // | `SOCK_STREAM` | TCP    |
        // | `0`           | 自动选择协议 |
        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        

        if (sockfd_ < 0) 
        {
            if (error_message) 
            {
                *error_message = "socket create failed";
            }
            return false;
        }
        
        int one = 1;
        ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
        ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        int sndbuf = 4 * 1024 * 1024;
        int rcvbuf = 4 * 1024 * 1024;
        ::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        ::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        // 设置服务器地址结构, 并初始化
        sockaddr_in addr {};
        // `sin_family`：地址族，IPv4 使用 AF_INET
        addr.sin_family = AF_INET;
        // `sin_port`：端口号，使用 htons 转换为网络字节序
        addr.sin_port = htons(port_);
        // `inet_pton`：将点分十进制字符串 IP 地址转换为二进制形式，存储在 `sin_addr` 中
        if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) 
        {
            if (error_message) 
            {
                *error_message = "invalid ip address";
            }
            Close();
            return false;
        }

        if (connect(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) 
        {
            if (error_message) 
            {
                *error_message = std::string("connect failed: ") + std::strerror(errno);
            }
            Close();
            return false;
        }
        return true;
    }

    void TcpImageClient::Close() 
    {
        if (sockfd_ >= 0) 
        {
            close(sockfd_);
            sockfd_ = -1;
        }
    }

    // 发送一张图片（JPEG）给服务器，请求处理，然后接收处理后的图片并解码。
    bool TcpImageClient::SendImage(const cv::Mat& input,
                               cv::Mat* output,
                               uint32_t request_id,
                               std::string* error_message) 
    {
        std::string ignored_json;
        return SendImageWithResult(input, output, &ignored_json, request_id, error_message);
    }
    bool TcpImageClient::SendAll(const uint8_t* data,
                             std::size_t size,
                             std::string* error_message) 
    {
        // 已发送的字节数
        std::size_t sent = 0;
        while (sent < size) 
        {
            if (!WaitFdReady(sockfd_, POLLOUT, kSendTimeoutMs, error_message))
            {
                return false;
            }

            const std::size_t chunk_size = std::min(kSendChunkBytes, size - sent);

            // data + sent → 从“还没发的地方”开始发送，size - sent → 还剩多少字节没发送
            // 0	普通发送	默认
            // MSG_NOSIGNAL	防止崩溃   强烈推荐
            // MSG_DONTWAIT	 不阻塞	  高性能/异步
            const ssize_t n = send(sockfd_, data + sent, chunk_size, MSG_NOSIGNAL | MSG_DONTWAIT);
            // n == 0 → 连接关闭
            // n < 0 → 出错（比如断网）
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            {
                continue;
            }

            if (n <= 0)
            {
                if (error_message) 
                {
                    *error_message = std::string("send failed: ") + std::strerror(errno);
                }
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool TcpImageClient::RecvExact(uint8_t* data,
                               std::size_t size,
                               std::string* error_message) 
    {
        std::size_t received = 0;
        while (received < size) 
        {
            if (!WaitFdReady(sockfd_, POLLIN, kRecvTimeoutMs, error_message))
            {
                return false;
            }

            const ssize_t n = recv(sockfd_, data + received, size - received, MSG_DONTWAIT);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            {
                continue;
            }

            if (n <= 0) 
            {
                if (error_message) {
                    *error_message = std::string("recv failed: ") + std::strerror(errno);
                }
                return false;
            }

            received += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool TcpImageClient::SendImageWithResult(const cv::Mat& input,
                                            cv::Mat* output,
                                            std::string* result_json,
                                            uint32_t request_id,
                                            std::string* error_message) 
    {
        cv::Mat ignored_depth_output;
        return SendImageWithResultAndDepth(
            input,
            output,
            &ignored_depth_output,
            result_json,
            request_id,
            error_message
        );
    }

    bool TcpImageClient::SendImageWithResultAndDepth(const cv::Mat& input,
                                            cv::Mat* output,
                                            cv::Mat* depth_output,
                                            std::string* result_json,
                                            uint32_t request_id,
                                            std::string* error_message)
    {
        if (!output || !result_json)
        {
            if (error_message)
            {
                *error_message = "output or result_json pointer is null";
            }
            return false;
        }

        if (!depth_output)
        {
            if (error_message)
            {
                *error_message = "depth_output pointer is null";
            }
            return false;
        }

        result_json->clear();
        depth_output->release();

        if (sockfd_ < 0 && !Connect(error_message)) 
        {
            return false;
        }

        std::vector<uint8_t> encoded;
        if (!EncodeJpeg(input, &encoded, 90, error_message)) 
        {
            return false;
        }

        std::vector<uint8_t> packet = BuildPacket(MessageType::kImageRequest, request_id, encoded);

        if (!SendAll(packet.data(), packet.size(), error_message)) 
        {
            return false;
        }

        std::vector<uint8_t> header_buf(kHeaderSize);
        if (!RecvExact(header_buf.data(), header_buf.size(), error_message)) 
        {
            return false;
        }

        auto header_opt = DeserializeHeader(header_buf.data(), header_buf.size());
        if (!header_opt.has_value()) 
        {
            if (error_message)
            {
                *error_message = "invalid response header";
            }
            return false;
        }

        const MessageHeader header = header_opt.value();

        std::vector<uint8_t> payload(header.payload_size);
        if (header.payload_size > 0 && !RecvExact(payload.data(), payload.size(), error_message)) 
        {
            return false;
        }

        if (static_cast<MessageType>(header.msg_type) == MessageType::kErrorResponse) 
        {
            if (error_message)
            {
                *error_message = std::string(payload.begin(), payload.end());
            }
            return false;
        }

        if (static_cast<MessageType>(header.msg_type) != MessageType::kImageResponse) 
        {
            if (error_message)
            {
                *error_message = "unexpected response msg_type";
            }
            return false;
        }

        std::vector<uint8_t> image_bytes;
        std::vector<uint8_t> depth_image_bytes;
        if (!UnpackResultPayloadWithDepth(payload,
                                          result_json,
                                          &image_bytes,
                                          &depth_image_bytes,
                                          error_message))
        {
            return false;
        }

        if (!DecodeImage(image_bytes, output, error_message))
        {
            return false;
        }

        if (!depth_image_bytes.empty())
        {
            return DecodeImage(depth_image_bytes, depth_output, error_message);
        }

        return true;
    }

} // namespace ris
