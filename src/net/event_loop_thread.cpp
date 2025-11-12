#include "net/event_loop_thread.h"

EventLoopThread::EventLoopThread(const std::string& name)
    : loop_(nullptr), thread_(), mutex_(), cond_(), name_(name), exiting_(false){}

EventLoopThread::~EventLoopThread(){
    exiting_ = true;
    if(loop_ != nullptr){
        loop_->quit();
        if(thread_.joinable()){
            thread_.join();
        }
    }
}

EventLoop* EventLoopThread::startLoop(){
    thread_ = std::thread(&EventLoopThread::threadFunc, this);

    EventLoop* loop = nullptr;
    {
        // 等待，直到新线程创建好EventLoop并将其指针赋值给loop_
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return loop_ != nullptr; }); // 请求资源，请求成功执行接下来的代码，即P操作
        loop = loop_;
    }
    return loop;
}

void EventLoopThread::threadFunc(){
    EventLoop loop; // 在栈上创建一个EventLoop，其生命周期与线程相同

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one(); // 通知startLoop(), loop_已经准备好了，随机唤醒一个等待线程
    }

    loop.loop(); // 启动事件循环，阻塞在这里直到quit()被调用

    // 线程即将退出，清理loop_指针
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}