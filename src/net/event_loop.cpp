#include "net/event_loop.h"
#include "net/channel.h"
#include "net/poller.h"
#include "net/timer.h"
#include <cassert>
#include <iostream>

// TLS(Thread Local Storage) 确保每个线程最多只有一个EventLoop实例
__thread EventLoop* t_loop_in_this_thread = nullptr;

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      thread_id_(std::this_thread::get_id()),
      poller_(new Poller(this)),
      timer_queue_(new TimerQueue(this)){
        if(t_loop_in_this_thread){
            // Log FATAL: Another EventLoop exists in this thread
            exit(1);
        }else{
            t_loop_in_this_thread = this;
        }
}

EventLoop::~EventLoop(){
    assert(!looping_);
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
        // doPendingFunctors(); // 处理挂起的任务
        // 处理到期的定时器
        timer_queue_->handleExpireTimers();
    }

    looping_ = false;
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
    // runInLoop(std::bind(&Poller::removeChannel, poller_.get(), channel));
}

void EventLoop::runInLoop(Functor cb){
    if(isInLoopThread()){
        cb();
    }else{
        // TODO跨线程调用，需要唤醒
    }
}

void EventLoop::queueInLoop(Functor cb){
    std::lock_guard<std::mutex> lock(mutex_);
    pending_functors_.push_back(std::move(cb));
    // TODO
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