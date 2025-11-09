#include "net/timer.h"
#include "net/event_loop.h"
#include <vector>

TimerQueue::TimerQueue(EventLoop* loop) : loop_(loop){}
TimerQueue::~TimerQueue() {}

void TimerQueue::addTimer(TimerCallback cb, Timestamp when){
    timers_.insert({when, std::make_unique<Timer>(std::move(cb), when)});
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
    std::vector<std::unique_ptr<Timer>> expired_timers;

    // 找出所有到期的定时器
    auto end = timers_.lower_bound({addTime(now, 0.000001), nullptr});
    for(auto it = timers_.begin(); it != end; ){
        // 使用extract挖出节点
        auto node_handle = timers_.extract(it++);
        expired_timers.push_back(std::move(node_handle.value().second));
    }

    // 调用到期定时器的回调
    for(const auto& entry : expired_timers){
        entry->run();
    }
}