#include "net/event_loop.h"
#include "net/channel.h"
#include "net/poller.h"
#include "connection.h"
#include "net/timer.h"
#include "utils/logger.h"
#include <cassert>
#include <iostream>
#include <sys/eventfd.h>
#include <unistd.h>

// TLS(Thread Local Storage) 确保每个线程最多只有一个EventLoop实例
__thread EventLoop* t_loop_in_this_thread = nullptr;

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      thread_id_(std::this_thread::get_id()),
      poller_(new Poller(this)),
      timer_queue_(new TimerQueue(this)),
      wakeup_fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)), // 创建eventfd
      wakeup_channel_(new Channel(this, wakeup_fd_)){
        if(t_loop_in_this_thread){
            // Log FATAL: Another EventLoop exists in this thread
            exit(1);
        }else{
            t_loop_in_this_thread = this;
        }
        // 设置wakeup_channel_的回调
        wakeup_channel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
        wakeup_channel_->enableReading(); // 始终监听wakeup_fd_上的事件
}

EventLoop::~EventLoop() {
    assert(!looping_);
    
    // 在析构前，移除 wakeup_channel_
    wakeup_channel_->disableAll();
    wakeup_channel_->remove();

    ::close(wakeup_fd_);
    t_loop_in_this_thread = nullptr;
}

void EventLoop::loop(){
    assert(!looping_);
    assertInLoopThread();
    looping_ = true;
    quit_ = false;

    while(!quit_){
        active_channels_.clear();
        // 计算poll的超时时间
        Timestamp earliest = timer_queue_->getEarliestExpiration();
        int64_t timeout_ms = -1;
        if(earliest.microSecondSinceEpoch() > 0){
            int64_t diff = earliest.microSecondSinceEpoch() - Timestamp::now().microSecondSinceEpoch();
            timeout_ms = (diff < 0) ? 0 : diff / 1000;
        }
        poller_->poll(static_cast<int>(timeout_ms), &active_channels_); // 10秒超时

        for(Channel* channel : active_channels_){
            channel->handleEvent();
        }
        doPendingFunctors(); // 处理完I/O事件后，处理挂起的任务
        // 处理到期的定时器
        timer_queue_->handleExpireTimers();
    }

    looping_ = false;
}

// eventfd的回调，只用于清空缓冲区，防止重复触发
void EventLoop::handleRead(){
    uint64_t one = 1;
    size_t n = ::read(wakeup_fd_, &one, sizeof(one));
    if(n != sizeof(one)){
        // Log ERROR
    }
}

void EventLoop::quit(){
    quit_ = true;
    // TODO 如果是在其他线程调用quit，可能需要唤醒loop，暂不实现
}

void EventLoop::updateChannel(Channel* channel){
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel){
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    poller_->removeChannel(channel);
}

void EventLoop::runInLoop(Functor cb){
    if(isInLoopThread()){
        cb(); // 如果是当前进程，直接执行
    }else{ // 否则，放入队列并唤醒
        // 跨线程调用，需要唤醒
        queueInLoop(std::move(cb)); 
    }
}

void EventLoop::queueInLoop(Functor cb){
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_functors_.push_back(std::move(cb));
    }
    
    // 只有在需要唤醒时才唤醒，如loop正在处理pending functors，此时加入新的functor就不需要唤醒
    // TODO 简化处理，总是唤醒
    wakeup();
}

void EventLoop::wakeup(){
    uint64_t one = 1;
    // 计数器从0变为1,触发可读事件
    ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
    if(n != sizeof(one)){
        // Log ERROR
    }
}

void EventLoop::doPendingFunctors(){
    std::vector<Functor> functors;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pending_functors_);
    }
    for(const Functor& functor : functors){
        functor();
    }
}

void EventLoop::abortNotInLoopThread(){
    std::cerr << "EventLop::abortNotInLoopThread() - EventLoop " << this
            << " was created in threadId_ " << thread_id_
            << ", current thread id = " << std::this_thread::get_id() << std::endl;
    exit(1);
}

TimerId EventLoop::runAt(Timestamp time, std::function<void()> cb){
    return timer_queue_->addTimer(std::move(cb), time);
}

TimerId EventLoop::runAfter(double delay, std::function<void()> cb){
    Timestamp time(addTime(Timestamp::now(), delay));
    return runAt(time, std::move(cb));
}

void EventLoop::cancel(TimerId timer_id){
    timer_queue_->cancel(timer_id);
}

void EventLoop::removeConnection(const ConnectionPtr& conn){
    // 在主loop中执行移除操作
    runInLoop(std::bind(&EventLoop::removeConnectionInLoop, this, conn));
}

void EventLoop::removeConnectionInLoop(const ConnectionPtr& conn){
    assertInLoopThread();
    int fd = conn->getFd();
    size_t n = connections_.erase(fd);
    assert(n == 1);

    // 此时从Channel触发的事件已经处理完毕，可以安全移除Channel
    queueInLoop([conn](){
        // 捕获了conn，延长其生命周期，lambda执行完毕后，conn被析构，从而Connection对象被销毁
        // 在此之前需移除Channel
        conn->getChannel()->remove();
    });
    // std::cout << "EventLoop " << this << " current connections (" << connections_.size() << "): [ ";
    // for (const auto& pair : connections_) {
    //     std::cout << pair.first << " ";
    // }
    LOG_INFO << "EventLoop " << this << " remove connection fd=" << fd;
    // std::cout << "]" << std::endl;
}

void EventLoop::addConnection(int fd, ConnectionPtr conn){
    assertInLoopThread();
    connections_[fd] = conn;
    LOG_INFO << "EventLoop " << this << " added connection fd=" << fd;
}