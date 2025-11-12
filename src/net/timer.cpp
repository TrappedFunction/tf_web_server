#include "net/timer.h"
#include "net/event_loop.h"
#include <vector>
#include <cassert>

TimerQueue::TimerQueue(EventLoop* loop) : loop_(loop){}
TimerQueue::~TimerQueue() {
    for(const auto& timer_ptr : active_timers_){
        delete timer_ptr;
    }
}

TimerId TimerQueue::addTimer(TimerCallback cb, Timestamp when){
    TimerId timer = new Timer(std::move(cb), when);
    loop_->runInLoop([this, timer, when](){
        auto result = active_timers_.insert(timer);
        assert(result.second);
        timers_.insert({when, timer});
    });
    return timer;
}

void TimerQueue::cancel(TimerId timer_id){
    loop_->runInLoop([this, timer_id](){
        if(active_timers_.count(timer_id)){
            // 从两个set中都移除
            timers_.erase({timer_id->expiration(), timer_id});
            active_timers_.erase(timer_id);
            // 释放内存
            delete timer_id;
        }
    });
}

Timestamp TimerQueue::getEarliestExpiration() const {
    if(timers_.empty()){
        return Timestamp(); // 返回一个无效时间戳
    }
    return timers_.begin()->first;
}

void TimerQueue::handleExpireTimers(){
    loop_->assertInLoopThread();
    Timestamp now = Timestamp::now();
    std::vector<TimerId> expired_timers;

    // 找出所有到期的定时器
    auto end = timers_.lower_bound({addTime(now, 0.000001), nullptr});
    for(auto it = timers_.begin(); it != end; it++){
        expired_timers.push_back(it->second);
    }

    // 从set中移除
    timers_.erase(timers_.begin(), end);
    for(const auto&timer : expired_timers){
        active_timers_.erase(timer);
    }


    // 调用到期定时器的回调
    for(const auto& entry : expired_timers){
        entry->run();
        delete entry;
    }
}