#include "net/event_loop_thread_pool.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop, const std::string& name, int num_threads)
    : base_loop_(base_loop), name_(name), num_threads_(num_threads), next_(0){}

EventLoopThreadPool::~EventLoopThreadPool(){
    // EventLoopThread的析构函数会处理线程的join
}

void EventLoopThreadPool::start(){
    base_loop_->assertInLoopThread();
    for(int i = 0; i < num_threads_; i++){
        std::string thread_name = name_ + std::to_string(i);
        EventLoopThread* t = new EventLoopThread(thread_name);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        loops_.push_back(t->startLoop()); // 启动线程并获取EventLoop指针
    }
}

EventLoop* EventLoopThreadPool::getNextLoop(){
    base_loop_->assertInLoopThread();
    // 如果线程池为空，所有连接都在主Reactor上处理
    if(loops_.empty()){
        return base_loop_;
    }

    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
    return loop;
}