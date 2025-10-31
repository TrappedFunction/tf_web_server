#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <functional>

class Channel;
class Poller;

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

private:
    void abortNotInLoopThread();

    using ChannelList = std::vector<Channel*>;

    bool looping_;
    bool quit_;
    const std::thread::id thread_id_; // 当前EventLoop所属的线程ID

    std::unique_ptr<Poller> poller_;
    ChannelList active_channels_;
};
