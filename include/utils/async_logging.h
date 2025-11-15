#pragma once
#include "utils/log_stream.h"
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class AsyncLogging{
public:
    AsyncLogging(const std::string& basename, off_t roll_size, int flush_interval = 3);
    ~AsyncLogging();

    void append(const char* logline, int len);

    void start();
    void stop();
private:
    void threadFunc();

    using Buffer = FixedBuffer<kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    const int flush_interval_;
    std::atomic<bool> running_;
    const std::string basename_;
    const off_t roll_size_; // 日志文件滚动大小
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    BufferPtr current_buffer_; // 前端当前正在写的缓冲区
    BufferPtr next_buffer_; // 备用缓冲区
    BufferVector buffers_; // 前端写入的缓冲区列表
};