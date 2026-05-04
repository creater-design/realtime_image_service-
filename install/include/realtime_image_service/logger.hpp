#pragma once

#include <chrono> // C++17 chrono库用于高精度计时
#include <ctime> // C++17 ctime库用于时间格式化
#include <iomanip> // C++17 iomanip库用于时间格式化
#include <iostream>
#include <sstream>
#include <string>

namespace ris 
{
    // 时间格式化
    inline std::string CurrentTimeString() 
    {
        const auto now = std::chrono::system_clock::now(); // 获取当前时间，从 1970-01-01 00:00:00 到现在的时间长度
        const auto time = std::chrono::system_clock::to_time_t(now); // 转换为time_t格式
        std::tm tm_now {}; // 定义一个tm结构体用于存储时间信息，并初始化为0
        localtime_r(&time, &tm_now); // 将time_t格式的时间转换为tm结构体，localtime_r是线程安全的版本

        std::ostringstream oss;
        oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
        return oss.str(); // 格式化时间为字符串，格式为 "YYYY-MM-DD HH:MM:SS"
    }

    // 日志系统
    inline void Log(const char* level, const std::string& message) 
    {
        std::cerr << "[" << CurrentTimeString() << "]"
                    << "[" << level << "] "
                    << message << std::endl;
    }

    // 自动计时
    class ScopedTimer 
    {
    public:
        explicit ScopedTimer(std::string name)
        : name_(std::move(name)), start_(std::chrono::steady_clock::now()) {} 

        ~ScopedTimer() 
        {
            const auto end = std::chrono::steady_clock::now();
            // 计算两个时间点之间的时间差，并以"微秒整数"的形式返回
            const auto cost_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
            Log("TIMER", name_ + " cost_us=" + std::to_string(cost_us));
        }

    private:
        std::string name_;
        std::chrono::steady_clock::time_point start_;
    };

    inline int64_t NowUs() 
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

}  // namespace ris

#define RIS_LOG_INFO(msg) ::ris::Log("INFO", (msg)) // 从全局命名空间开始找 ris::Log
#define RIS_LOG_WARN(msg) ::ris::Log("WARN", (msg))
#define RIS_LOG_ERROR(msg) ::ris::Log("ERROR", (msg))
#define RIS_SCOPE_TIMER(name) ::ris::ScopedTimer scoped_timer_##__LINE__((name)) // 定义一个局部变量，名字由 "scoped_timer_" 和当前行号组成，确保每个计时器变量名唯一
