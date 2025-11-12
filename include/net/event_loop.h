#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <functional>
#include "net/timer.h"
#include "utils/timestamp.h"

class Channel;
class Poller;
class TimerQueue;
class Timestamp;

class EventLoop{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 启动事件循环
    void loop();
    // 退出事件循环
    void quit();

    // 更新channel
    void updateChannel(Channel* channel);
    // 移除channel
    void removeChannel(Channel* channel);

    // 断言当前是否在EventLoop自己的线程中
    void assertInLoopThread(){
        if(!isInLoopThread()){
            abortNotInLoopThread();
        }
    }

    bool isInLoopThread() const { return thread_id_ == std::this_thread::get_id(); }
    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    // 在指定时间运行回调
    TimerId runAt(Timestamp time, std::function<void()> cb);
    // 在N秒后运行回调
    TimerId runAfter(double delay, std::function<void()> cb);
    // 取消定时器连接
    void cancel(TimerId timer_id);

private:
    void abortNotInLoopThread();
    void doPendingFunctors();
    

    using ChannelList = std::vector<Channel*>;

    bool looping_;
    bool quit_;
    const std::thread::id thread_id_; // 当前EventLoop所属的线程ID

    std::unique_ptr<Poller> poller_;
    ChannelList active_channels_;
    std::vector<Functor> pending_functors_;
    std::mutex mutex_;
    std::unique_ptr<TimerQueue> timer_queue_;
};
