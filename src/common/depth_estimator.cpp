#include "realtime_image_service/depth_estimator.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "realtime_image_service/image_codec.hpp"

namespace ris
{
    namespace
    {
        constexpr std::size_t kChunkBytes = 64 * 1024;
        constexpr std::size_t kMaxDepthPayloadSize = 20 * 1024 * 1024;

        void WriteUint32(uint8_t* dst, uint32_t value)
        {
            const uint32_t net_value = htonl(value);
            std::memcpy(dst, &net_value, sizeof(net_value));
        }

        uint32_t ReadUint32(const uint8_t* src)
        {
            uint32_t net_value = 0;
            std::memcpy(&net_value, src, sizeof(net_value));
            return ntohl(net_value);
        }

        void SetSocketOptions(int fd)
        {
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            int buffer_size = 4 * 1024 * 1024;
            ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
            ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
        }
    }

    DepthEstimator::DepthEstimator(std::string host, uint16_t port, int timeout_ms)
    : host_(std::move(host)), port_(port), timeout_ms_(timeout_ms)
    {
    }

    DepthEstimator::~DepthEstimator()
    {
        Close();
    }

    void DepthEstimator::Close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseLocked();
    }

    bool DepthEstimator::EstimateDepth(const cv::Mat& input_bgr,
                                       cv::Mat* depth_norm,
                                       std::string* error_message)
    {
        if (!depth_norm)
        {
            if (error_message) *error_message = "depth_norm pointer is null";
            return false;
        }

        depth_norm->release();

        std::vector<uint8_t> encoded;
        if (!EncodeJpeg(input_bgr, &encoded, 90, error_message))
        {
            return false;
        }

        uint32_t status = 1;
        std::vector<uint8_t> payload;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!EnsureConnectedLocked(error_message))
            {
                return false;
            }

            if (!ExchangeLocked(encoded, &status, &payload, error_message))
            {
                CloseLocked();
                std::string reconnect_error;
                if (!EnsureConnectedLocked(&reconnect_error) ||
                    !ExchangeLocked(encoded, &status, &payload, error_message))
                {
                    if (error_message && !reconnect_error.empty())
                    {
                        *error_message += "; reconnect failed: " + reconnect_error;
                    }
                    CloseLocked();
                    return false;
                }
            }
        }

        if (status != 0)
        {
            if (error_message) *error_message = std::string(payload.begin(), payload.end());
            return false;
        }

        cv::Mat depth_u8 = cv::imdecode(payload, cv::IMREAD_GRAYSCALE);
        if (depth_u8.empty())
        {
            if (error_message) *error_message = "failed to decode depth image from depth_server";
            return false;
        }

        depth_u8.convertTo(*depth_norm, CV_32FC1, 1.0 / 255.0);
        return true;
    }

    bool DepthEstimator::EnsureConnectedLocked(std::string* error_message)
    {
        if (sockfd_ >= 0)
        {
            return true;
        }

        return ConnectLocked(error_message);
    }

    bool DepthEstimator::ConnectLocked(std::string* error_message)
    {
        CloseLocked();

        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            if (error_message)
            {
                *error_message = std::string("depth socket create failed: ") + std::strerror(errno);
            }
            return false;
        }

        SetSocketOptions(fd);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0)
        {
            if (error_message) *error_message = "invalid depth server host: " + host_;
            ::close(fd);
            return false;
        }

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            if (error_message)
            {
                *error_message = std::string("connect depth_server failed: ") + std::strerror(errno);
            }
            ::close(fd);
            return false;
        }

        sockfd_ = fd;
        return true;
    }

    void DepthEstimator::CloseLocked()
    {
        if (sockfd_ >= 0)
        {
            ::close(sockfd_);
            sockfd_ = -1;
        }
    }

    bool DepthEstimator::ExchangeLocked(const std::vector<uint8_t>& encoded_image,
                                        uint32_t* status,
                                        std::vector<uint8_t>* payload,
                                        std::string* error_message)
    {
        if (!status || !payload)
        {
            if (error_message) *error_message = "depth response output pointer is null";
            return false;
        }

        payload->clear();

        uint8_t size_buf[4];
        WriteUint32(size_buf, static_cast<uint32_t>(encoded_image.size()));
        if (!SendAllLocked(size_buf, sizeof(size_buf), error_message) ||
            !SendAllLocked(encoded_image.data(), encoded_image.size(), error_message))
        {
            return false;
        }

        uint8_t response_header[8];
        if (!RecvExactLocked(response_header, sizeof(response_header), error_message))
        {
            return false;
        }

        *status = ReadUint32(response_header);
        const uint32_t payload_size = ReadUint32(response_header + 4);
        if (payload_size > kMaxDepthPayloadSize)
        {
            if (error_message) *error_message = "depth response payload too large";
            return false;
        }

        payload->resize(payload_size);
        if (payload_size > 0 && !RecvExactLocked(payload->data(), payload->size(), error_message))
        {
            return false;
        }

        return true;
    }

    bool DepthEstimator::SendAllLocked(const uint8_t* data,
                                       std::size_t size,
                                       std::string* error_message)
    {
        std::size_t sent = 0;
        while (sent < size)
        {
            if (!WaitFdReadyLocked(POLLOUT, error_message))
            {
                return false;
            }

            const std::size_t chunk_size = std::min(kChunkBytes, size - sent);
            const ssize_t n = ::send(sockfd_, data + sent, chunk_size, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n <= 0)
            {
                if (error_message)
                {
                    *error_message = std::string("depth send failed: ") + std::strerror(errno);
                }
                return false;
            }

            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool DepthEstimator::RecvExactLocked(uint8_t* data,
                                         std::size_t size,
                                         std::string* error_message)
    {
        std::size_t received = 0;
        while (received < size)
        {
            if (!WaitFdReadyLocked(POLLIN, error_message))
            {
                return false;
            }

            const ssize_t n = ::recv(sockfd_, data + received, size - received, 0);
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n <= 0)
            {
                if (error_message)
                {
                    *error_message = std::string("depth recv failed: ") + std::strerror(errno);
                }
                return false;
            }

            received += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool DepthEstimator::WaitFdReadyLocked(short events,
                                           std::string* error_message) const
    {
        pollfd pfd{};
        pfd.fd = sockfd_;
        pfd.events = events;

        while (true)
        {
            const int rc = ::poll(&pfd, 1, timeout_ms_);
            if (rc > 0)
            {
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                {
                    if (error_message) *error_message = "depth socket error/hup";
                    return false;
                }
                return true;
            }
            if (rc == 0)
            {
                if (error_message) *error_message = "depth socket timeout";
                return false;
            }
            if (errno == EINTR)
            {
                continue;
            }

            if (error_message)
            {
                *error_message = std::string("depth poll failed: ") + std::strerror(errno);
            }
            return false;
        }
    }
}
