#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <string>
#include "realtime_image_service/cuda_kernels.hpp"

namespace ris 
{

namespace 
{
    int64_t NowUsLocal() 
    {
      return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
    }

	__global__ void SobelKernel(const uint8_t* input, // 输入灰度图
								uint8_t* output,
								int width,
								int height,
								int input_step,  // 输入图像每一行实际占多少字节
								int output_step) // 输出图像每一行实际占多少字节
	{
		const int x = blockIdx.x * blockDim.x + threadIdx.x;
		const int y = blockIdx.y * blockDim.y + threadIdx.y;

		if (x <= 0 || y <= 0 || x >= width - 1 || y >= height - 1) 
		{
			return;
		}

		// 获取当前像素点周围9个点的灰度值
		const int tl = input[(y - 1) * input_step + (x - 1)];
		const int tc = input[(y - 1) * input_step + x];
		const int tr = input[(y - 1) * input_step + (x + 1)];
		const int ml = input[y * input_step + (x - 1)];
		const int mr = input[y * input_step + (x + 1)];
		const int bl = input[(y + 1) * input_step + (x - 1)];
		const int bc = input[(y + 1) * input_step + x];
		const int br = input[(y + 1) * input_step + (x + 1)];

		// x方向的卷积核
		// -1  0  +1
		// -2  0  +2
		// -1  0  +1
		const int gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
		// y方向的卷积核
		// -1  -2  -1
		//  0   0   0
		// +1  +2  +1
		const int gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
		// 计算像素点的梯度
		const int magnitude = min(255, abs(gx) + abs(gy));
		output[y * output_step + x] = static_cast<uint8_t>(magnitude);
	}

}  // namespace

	bool RunSobelCudaKernel(const uint8_t* input,
							int width,
							int height,
							int input_step,
							uint8_t* output,
							int output_step,
							ProcessMetrics* metrics,
							std::string* error_message) 
	{
		#if !RIS_ENABLE_CUDA
		if (error_message) 
		{
			*error_message = "cuda support is disabled";
		}
		return false;

		#else
		if (!input || !output) 
		{
			if (error_message) 
			{
				*error_message = "input or output buffer is null";
			}
			return false;
		}

		// 分配显存并复制数据到设备
		const std::size_t input_bytes = static_cast<std::size_t>(input_step)   * static_cast<std::size_t>(height);
		const std::size_t output_bytes = static_cast<std::size_t>(output_step) * static_cast<std::size_t>(height);

		uint8_t* d_input = nullptr;
		uint8_t* d_output = nullptr;
		cudaError_t err = cudaMalloc(&d_input, input_bytes);
		if (err != cudaSuccess) 
		{
			if (error_message) 
			{
				*error_message = "cudaMalloc input failed";
			}
			return false;
		}

		err = cudaMalloc(&d_output, output_bytes);
		if (err != cudaSuccess) 
		{
			cudaFree(d_input);
			if (error_message) 
			{
				*error_message = "cudaMalloc output failed";
			}
			return false;
		}

		const int64_t h2d_begin = NowUsLocal();
		// 将数据从主机复制到设备
		err = cudaMemcpy(d_input, input, input_bytes, cudaMemcpyHostToDevice);
		if (err != cudaSuccess) 
		{
			cudaFree(d_input);
			cudaFree(d_output);
			if (error_message) 
			{
				*error_message = "cudaMemcpy H2D failed";
			}
			return false;
		}
		const int64_t h2d_end = NowUsLocal();

		err = cudaMemset(d_output, 0, output_bytes);
		if (err != cudaSuccess) 
		{
			cudaFree(d_input);
			cudaFree(d_output);
			if (error_message) 
			{
				*error_message = "cudaMemset failed";
			}
			return false;
		}

		const dim3 block(16, 16); // 每个block包含16x16个线程
		const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y); // 计算需要多少个block来覆盖整个图像，x与y均向上取整
		const int64_t compute_begin = NowUsLocal();
		// 运行核函数
		SobelKernel<<<grid, block>>>(d_input, d_output, width, height, input_step, output_step);

		err = cudaGetLastError(); // 检查核函数调用是否成功
		if (err != cudaSuccess) 
		{
			cudaFree(d_input); 
			cudaFree(d_output);
			if (error_message) 
			{
				*error_message = "kernel launch failed";
			}
			return false;
		}

		err = cudaDeviceSynchronize(); // 等待核函数执行完成并检查是否有错误
		// 如果有错误，则返回错误信息
		if (err != cudaSuccess) 
		{
			cudaFree(d_input);
			cudaFree(d_output);
			if (error_message) 
			{
				*error_message = "cudaDeviceSynchronize failed";
			}
			return false;
		}
		const int64_t compute_end = NowUsLocal();

		const int64_t d2h_begin = NowUsLocal();
		// 将数据从设备复制到主机
		err = cudaMemcpy(output, d_output, output_bytes, cudaMemcpyDeviceToHost);
		cudaFree(d_input);
		cudaFree(d_output);
		if (err != cudaSuccess) 
		{
			if (error_message) 
			{
			*error_message = "cudaMemcpy D2H failed";
			}
			return false;
		}
		const int64_t d2h_end = NowUsLocal();

		if (metrics) 
		{
			metrics->transfer_h2d_us = h2d_end - h2d_begin;
			metrics->compute_us = compute_end - compute_begin;
			metrics->transfer_d2h_us = d2h_end - d2h_begin;
		}
		return true;
		#endif
	}

}  // namespace ris
