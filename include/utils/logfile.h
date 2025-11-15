#pragma once
#include <string>
#include <memory>
#include <mutex>
#include "socket.h"
#include "utils/timestamp.h" // 需要时间戳来生成文件名

class LogFile : NonCopyable {
public:
    LogFile(const std::string& basename,
            off_t roll_size,
            int flush_interval = 3,
            int check_every_n = 1024);
    ~LogFile();

    void append(const char* logline, int len);
    void flush();

private:
    void append_unlocked(const char* logline, int len);

    // 根据当前时间和进程ID生成日志文件名
    static std::string getLogFileName(const std::string& basename, time_t* now);
    
    // 滚动日志文件
    void rollFile();

    const std::string basename_;
    const off_t roll_size_;
    const int flush_interval_;
    const int check_every_n_;

    int count_;
    
    std::unique_ptr<std::mutex> mutex_;
    time_t start_of_period_; // 用于按天滚动
    time_t last_roll_;
    time_t last_flush_;
    FILE* file_;

    const static int kRollPerSeconds_ = 60 * 60 * 24; // 按天滚动
};