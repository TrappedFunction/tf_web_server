#pragma once
#include "utils/timestamp.h"
#include <functional>
#include <memory>
#include <set>

// 向前声明
class EventLoop;

class Timer{
public:
    using TimerCallback = std::function<void()>;
    Timer(TimerCallback cb, Timestamp when) : callback_(std::move(cb)), expiration_(when) {}
    void run() const { callback_(); }
    Timestamp expiration() const { return expiration_; }
private:
    const TimerCallback callback_;
    Timestamp expiration_;
};

using TimerId = Timer*;

class TimerQueue{
public:
    using TimerCallback = std::function<void()>;

    TimerQueue(EventLoop* loop);
    ~TimerQueue();

    // 添加定时器,返回一个TimerId
    TimerId addTimer(TimerCallback cb, Timestamp when);

    // 取消一个定时器
    void cancel(TimerId timer_id);

    // 获取并处理所有到期的定时器
    void handleExpireTimers();

    // 获取下一个定时器的到期时间
    Timestamp getEarliestExpiration() const;


private:
    using Entry = std::pair<Timestamp, TimerId>;
    using TimerList = std::set<Entry>;
    using ActiveTimers = std::set<TimerId>; // 快速查找TimerId是否存在

    EventLoop* loop_;
    TimerList timers_; // 作为底层的优先队列，会根据Timestamp排序
    ActiveTimers active_timers_;
};