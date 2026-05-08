#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <arpa/inet.h>
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
    constexpr int kSendTimeoutMs = 10000;
    constexpr int kRecvTimeoutMs = 10000;
    constexpr std::size_t kSendChunkBytes = 64 * 1024;

    bool WaitFdReady(int fd, short events, int timeout_ms, std::string* error_message) 
    {
        // Bound send/recv waits so a broken server cannot block the ROS worker forever.
        pollfd pfd {};
        pfd.fd = fd;
        pfd.events = events;

        while (true) 
        {
            const int rc = ::poll(&pfd, 1, timeout_ms);
            if (rc > 0) 
            {   
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) 
                {
                    if (error_message) *error_message = "socket error/hup";
                    return false;
                }
                return true;
            }

            if (rc == 0) 
            {
                if (error_message) *error_message = "socket wait timeout";
                return false;
            }

            if (errno == EINTR) continue;

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

        sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
        

        if (sockfd_ < 0) 
        {
            if (error_message) 
            {
                *error_message = "socket create failed";
            }
            return false;
        }

        // Keep the persistent connection responsive for image payload exchange.
        int one = 1;
        ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
        ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        int sndbuf = 4 * 1024 * 1024;
        int rcvbuf = 4 * 1024 * 1024;
        ::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        ::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
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
        std::size_t sent = 0;
        while (sent < size) 
        {
            if (!WaitFdReady(sockfd_, POLLOUT, kSendTimeoutMs, error_message))
            {
                return false;
            }

            const std::size_t chunk_size = std::min(kSendChunkBytes, size - sent);

            // MSG_NOSIGNAL prevents SIGPIPE if the peer closes the connection.
            const ssize_t n = send(sockfd_, data + sent, chunk_size, MSG_NOSIGNAL | MSG_DONTWAIT);
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
        // Protocol parsing needs exact field sizes; partial TCP reads are absorbed here.
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

        // One application request = protocol header + JPEG payload.
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

        // Response payload contains JSON + processed image + optional depth image.
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
