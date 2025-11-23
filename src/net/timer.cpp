#include "net/timer.h"
#include "net/event_loop.h"
#include <vector>
#include <cassert>

TimerQueue::TimerQueue(EventLoop* loop) : loop_(loop){}
TimerQueue::~TimerQueue() {}

TimerId TimerQueue::addTimer(TimerCallback cb, Timestamp when){
    std::shared_ptr<Timer> timer = std::make_shared<Timer>(std::move(cb), when);
    loop_->runInLoop([this, timer, when](){
        timers_.insert({when, timer});
        // insert 到 active_timers_，这里会自动转换 shared_ptr 到 weak_ptr
        active_timers_.insert(timer);
    });
    return timer;
}

void TimerQueue::cancel(TimerId timer_id) {
    loop_->runInLoop([this, timer_id]() {
        // 1. 检查 active_timers_ 中是否存在
        auto it = active_timers_.find(timer_id);
        if (it != active_timers_.end()) {
            // 2. 尝试锁定获取 Timer 对象
            // 注意：因为 active_timers_ 里存的是 weak_ptr，我们需要 lock 才能拿到对象的属性（如 expiration）
            // 但此时我们已经确定它在 active_timers_ 里，且 timers_ 持有 shared_ptr，所以 lock 应该成功
            std::shared_ptr<Timer> timer = it->lock();
            if (timer) {
                // 3. 从 timers_ 中移除
                // 需要构造 Entry 进行查找
                auto entry_it = timers_.find({timer->expiration(), timer});
                if (entry_it != timers_.end()) {
                    timers_.erase(entry_it);
                }
            }
            // 4. 从 active_timers_ 中移除
            active_timers_.erase(it);
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
    std::vector<Entry> expired;

    // 找到所有过期的
    // 这里的哨兵使用 UINTPTR_MAX 强制让它排在相同时间的最后，保证取出所有当前时间的定时器
    Entry sentry(addTime(now, 0.000001), nullptr);
    auto end = timers_.lower_bound(sentry);
    
    // 移动到局部容器
    std::copy(timers_.begin(), end, std::back_inserter(expired));
    timers_.erase(timers_.begin(), end);

    // 执行回调
    for (const auto& entry : expired) {
        // **从 active_timers_ 中移除**
        // entry.second 是 shared_ptr，可以用来在 set<weak_ptr> 中查找
        active_timers_.erase(entry.second);
        
        if (entry.second) {
            entry.second->run();
        }
    }
    // 离开作用域后，expired 销毁，shared_ptr 计数减一，Timer 自动析构
}