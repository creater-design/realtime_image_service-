#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

namespace ris
{
    class TcpImageClient
    {
    public:
        TcpImageClient(std::string host, uint16_t port);
        ~TcpImageClient();

        // 连接到服务器
        bool Connect(std::string* error_message);
        // 关闭连接
        void Close();
        // 发送图片并接收处理结果
        bool SendImage(const cv::Mat& input,
                        cv::Mat* output,
                        uint32_t request_id,
                        std::string* error_message);

    private:
        // 发送所有数据，直到全部发送成功或发生错误
        bool SendAll(const uint8_t* data, std::size_t size, std::string* error_message);
        // 精确接收指定字节数的数据
        bool RecvExact(uint8_t* data, std::size_t size, std::string* error_message);
        
        // 保存服务器地址和端口，以及套接字文件描述符
        std::string host_;
        uint16_t port_;
        int sockfd_ = -1;

    };

} // namespace ris
