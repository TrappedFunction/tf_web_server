#include "utils/async_logging.h"
#include "utils/logfile.h"
#include <cstdio>
#include <cassert>
#include <chrono>

AsyncLogging::AsyncLogging(const std::string& basename, off_t roll_size, int flush_interval)
    : flush_interval_(flush_interval),
      running_(false),
      basename_(basename),
      roll_size_(roll_size),
      thread_(),
      mutex_(),
      cond_(),
      current_buffer_(new Buffer),
      next_buffer_(new Buffer),
      buffers_() {
    current_buffer_->bzero();
    next_buffer_->bzero();
    buffers_.reserve(16);
}

AsyncLogging::~AsyncLogging() {
    if (running_) {
        stop();
    }
}

void AsyncLogging::append(const char* logline, int len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_buffer_->avail() > len) {
        current_buffer_->append(logline, len);
    } else {
        buffers_.push_back(std::move(current_buffer_));
        if (next_buffer_) {
            current_buffer_ = std::move(next_buffer_);
        } else {
            current_buffer_.reset(new Buffer);
        }
        current_buffer_->append(logline, len);
        cond_.notify_one();
    }
}

void AsyncLogging::start() {
    running_ = true;
    thread_ = std::thread(&AsyncLogging::threadFunc, this);
}

void AsyncLogging::stop() {
    running_ = false;
    cond_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void AsyncLogging::threadFunc() {
    assert(running_ == true);
    
    // 使用我们刚实现的 LogFile
    LogFile output(basename_, roll_size_);
    
    BufferPtr new_buffer1(new Buffer);
    BufferPtr new_buffer2(new Buffer);
    new_buffer1->bzero();
    new_buffer2->bzero();
    
    BufferVector buffers_to_write;
    buffers_to_write.reserve(16);

    while (running_) {
        assert(new_buffer1 && new_buffer1->length() == 0);
        assert(new_buffer2 && new_buffer2->length() == 0);
        assert(buffers_to_write.empty());

        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (buffers_.empty()) {
                cond_.wait_for(lock, std::chrono::seconds(flush_interval_));
            }
            buffers_.push_back(std::move(current_buffer_));
            current_buffer_ = std::move(new_buffer1);
            buffers_to_write.swap(buffers_);
            if (!next_buffer_) {
                next_buffer_ = std::move(new_buffer2);
            }
        }

        if (!buffers_to_write.empty()) {
            // 将所有待写入的日志写入文件
            for (const auto& buffer : buffers_to_write) {
                output.append(buffer->data(), buffer->length());
            }

            // 将多余的空闲缓冲区归还，只保留两个备用
            if (buffers_to_write.size() > 2) {
                buffers_to_write.resize(2);
            }
            
            // 补充 new_buffer1
            if (!new_buffer1) {
                assert(!buffers_to_write.empty());
                new_buffer1 = std::move(buffers_to_write.back());
                buffers_to_write.pop_back();
                new_buffer1->reset();
            }

            // 补充 new_buffer2
            if (!new_buffer2) {
                assert(!buffers_to_write.empty());
                new_buffer2 = std::move(buffers_to_write.back());
                buffers_to_write.pop_back();
                new_buffer2->reset();
            }

            buffers_to_write.clear();
            output.flush();
        }
    }

    // ***** 退出前最后一次刷盘 *****
    output.flush();
    
    // 如果前端还有未处理的日志，也一并写入
    std::lock_guard<std::mutex> lock(mutex_);
    if (!buffers_.empty()) {
        for (const auto& buffer : buffers_) {
            output.append(buffer->data(), buffer->length());
        }
    }
    if (current_buffer_ && current_buffer_->length() > 0) {
        output.append(current_buffer_->data(), current_buffer_->length());
    }
    
    output.flush();
}