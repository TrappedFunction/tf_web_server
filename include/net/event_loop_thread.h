#pragma once
#include "net/event_loop.h"
#include "socket.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>

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