#include "realtime_image_service/depth_estimator.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "realtime_image_service/image_codec.hpp"

namespace ris
{
    namespace
    {
        constexpr std::size_t kChunkBytes = 64 * 1024;

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
    }

    DepthEstimator::DepthEstimator(std::string host, uint16_t port, int timeout_ms)
    : host_(std::move(host)), port_(port), timeout_ms_(timeout_ms)
    {
    }

    bool DepthEstimator::EstimateDepth(const cv::Mat& input_bgr,
                                       cv::Mat* depth_norm,
                                       std::string* error_message) const
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

        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            if (error_message) *error_message = std::string("depth socket create failed: ") + std::strerror(errno);
            return false;
        }

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

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
            if (error_message) *error_message = std::string("connect depth_server failed: ") + std::strerror(errno);
            ::close(fd);
            return false;
        }

        uint8_t size_buf[4];
        WriteUint32(size_buf, static_cast<uint32_t>(encoded.size()));
        if (!SendAll(fd, size_buf, sizeof(size_buf), error_message) ||
            !SendAll(fd, encoded.data(), encoded.size(), error_message))
        {
            ::close(fd);
            return false;
        }

        uint8_t response_header[8];
        if (!RecvExact(fd, response_header, sizeof(response_header), error_message))
        {
            ::close(fd);
            return false;
        }

        const uint32_t status = ReadUint32(response_header);
        const uint32_t payload_size = ReadUint32(response_header + 4);

        std::vector<uint8_t> payload(payload_size);
        if (payload_size > 0 && !RecvExact(fd, payload.data(), payload.size(), error_message))
        {
            ::close(fd);
            return false;
        }

        ::close(fd);

        if (status != 0)
        {
            if (error_message) *error_message = std::string(payload.begin(), payload.end());
            return false;
        }

        cv::Mat depth_u8 = cv::imdecode(payload, cv::IMREAD_GRAYSCALE);
        if (depth_u8.empty())
        {
            if (error_message) *error_message = "failed to decode depth png from depth_server";
            return false;
        }

        depth_u8.convertTo(*depth_norm, CV_32FC1, 1.0 / 255.0);
        return true;
    }

    bool DepthEstimator::SendAll(int fd,
                                 const uint8_t* data,
                                 std::size_t size,
                                 std::string* error_message) const
    {
        std::size_t sent = 0;
        while (sent < size)
        {
            if (!WaitFdReady(fd, POLLOUT, error_message))
            {
                return false;
            }

            const std::size_t chunk_size = std::min(kChunkBytes, size - sent);
            const ssize_t n = ::send(fd, data + sent, chunk_size, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n <= 0)
            {
                if (error_message) *error_message = std::string("depth send failed: ") + std::strerror(errno);
                return false;
            }

            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool DepthEstimator::RecvExact(int fd,
                                   uint8_t* data,
                                   std::size_t size,
                                   std::string* error_message) const
    {
        std::size_t received = 0;
        while (received < size)
        {
            if (!WaitFdReady(fd, POLLIN, error_message))
            {
                return false;
            }

            const ssize_t n = ::recv(fd, data + received, size - received, 0);
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n <= 0)
            {
                if (error_message) *error_message = std::string("depth recv failed: ") + std::strerror(errno);
                return false;
            }

            received += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool DepthEstimator::WaitFdReady(int fd,
                                     short events,
                                     std::string* error_message) const
    {
        pollfd pfd{};
        pfd.fd = fd;
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

            if (error_message) *error_message = std::string("depth poll failed: ") + std::strerror(errno);
            return false;
        }
    }
}
