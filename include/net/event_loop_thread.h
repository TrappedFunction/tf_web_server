#pragma once
#include "net/event_loop.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>

class NonCopyable{
public:
    NonCopyable() = default;
    ~NonCopyable() = default;
private:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

class EventLoopThread : NonCopyable{
public:
    EventLoopThread(const std::string& name = std::string());
    ~EventLoopThread();

    // 启动线程，并返回在新线程中创建的EventLoop指针
    EventLoop* startLoop();
private:
    void threadFunc();

    EventLoop* loop_; // 指向在新线程中创建的EventLoop对象
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::string name_;
    bool exiting_;
};