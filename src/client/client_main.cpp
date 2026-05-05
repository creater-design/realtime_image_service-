#include <cstdint>
#include <iostream>
#include <string>

#include <opencv2/opencv.hpp>

#include "realtime_image_service/logger.hpp"
#include "realtime_image_service/tcp_image_client.hpp"

int main(int argc, char* argv[])
{ 
    std::string host = "127.0.0.1";
    uint16_t port = 9999;
    std::string input_path = "samples/test.jpg";
    std::string output_path = "samples/result.jpg";
    // 解析命令行参数，覆盖默认的服务器地址、端口、输入输出图片路径等配置项。
    for (int i = 1; i < argc; ++i) 
    {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) 
        {
            host = argv[++i];
        } 
        else if (arg == "--port" && i + 1 < argc) 
        {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } 
        else if (arg == "--input" && i + 1 < argc) 
        {
            input_path = argv[++i];
        } 
        else if (arg == "--output" && i + 1 < argc) 
        {
            output_path = argv[++i];
        }
    }

    // 使用 OpenCV 读取输入图片，得到一个 cv::Mat 对象。如果读取失败（比如文件不存在），就直接返回错误。
    cv::Mat input = cv::imread(input_path, cv::IMREAD_COLOR);
    if (input.empty()) 
    {
        RIS_LOG_ERROR("failed to read input image: " + input_path);
        return 1;
    }

    const int64_t total_begin = ris::NowUs();
    ris::TcpImageClient client(host, port);
    std::string error_message;
    if (!client.Connect(&error_message)) 
    {
        RIS_LOG_ERROR(error_message);
        return 1;
    }

    cv::Mat output;
    // 将输入图片发送给服务器，并接收处理后的图片。如果过程中发生任何错误（比如网络断开、服务器返回错误响应、解码失败等），都直接返回错误。
    if (!client.SendImage(input, &output, 1, &error_message)) 
    {
        RIS_LOG_ERROR(error_message);
        return 1;
    }
    const int64_t network_total_us = ris::NowUs() - total_begin;

    const int64_t save_begin = ris::NowUs();
    if (!cv::imwrite(output_path, output)) 
    {
        RIS_LOG_ERROR("failed to write output image");
        return 1;
    }
    const int64_t save_us = ris::NowUs() - save_begin;

    RIS_LOG_INFO("result saved to " + output_path);
    RIS_LOG_INFO("client_metrics network_total_us=" + std::to_string(network_total_us) +
                " save_us=" + std::to_string(save_us) +
                " output=" + std::to_string(output.cols) + "x" + std::to_string(output.rows));
}