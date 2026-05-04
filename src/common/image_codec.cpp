#include "realtime_image_service/image_codec.hpp"

namespace ris
{
    bool EncodeJpeg(const cv::Mat& image,
                    std::vector<uint8_t>* output,
                    int quality,
                    std::string* error_message)
    {
        if (!output)
        {
            if(error_message)
                *error_message = "Output pointer is null.";
            return false;
        }

        if (image.empty())
        {
            if(error_message)
                *error_message = "Input image is empty.";
            return false;
        }

        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, quality};
        if (!cv::imencode(".jpg", image, *output, params))
        {
            if(error_message)
                *error_message = "Failed to encode image.";
            return false;
        }
        return true;

    }

    bool DecodeImage(const std::vector<uint8_t>& input,
                    cv::Mat* image,
                    std::string* error_message)
    {
        if (!image)
        {
            if(error_message)
                *error_message = "Image pointer is null.";
            return false;
        }

        if (input.empty())
        {
            if(error_message)
                *error_message = "Input data is empty.";
            return false;
        }

        *image = cv::imdecode(input, cv::IMREAD_COLOR);
        if (image->empty())
        {
            if(error_message)
                *error_message = "Failed to decode image.";
            return false;
        }
        return true;
    }

} // namespace ris
