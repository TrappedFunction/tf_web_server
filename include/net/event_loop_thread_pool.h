#pragma once
#include "net/event_loop.h"
#include "net/event_loop_thread.h"
#include <vector>
#include <memory>
#include <string>


// 负责创建、启动和分发从线程的EventLoop
class EventLoopThreadPool : NonCopyable{
public:
    EventLoopThreadPool(EventLoop* base_loop, const std::string& name, int num_threads);
    ~EventLoopThreadPool();

    void start();

    // 通过轮询算法获取下一个I/O EventLoop
    EventLoop* getNextLoop();
private:
    EventLoop* base_loop_; // 主Reactor，即接收请求所在的loop
    std::string name_;
    int num_threads_;
    int next_; // 轮询的索引
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_; // 存储所有从Reactor的指针
};