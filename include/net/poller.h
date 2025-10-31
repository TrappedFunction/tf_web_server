#pragma once
#include <vector>
#include <map>
#include "event_loop.h"

// 直接封装epoll的API，只需指导epoll_event和fd

// 向前声明Channel，避免循环引用
class Channel;

// Poller 是 EvenLoop的间接成员，生命周期由其控制
class Poller{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop* loop);
    ~Poller();

    // 核心，调用epoll_wait，获取活跃的事件
    void poll(int timeout_ms, ChannelList* active_channels);

    // 更新channel中的监听事件
    void updateChannel(Channel* channel);

    // 从poller中移除channel
    void removeChannel(Channel* channel);

    // 断言：确保当前线程是该Poller所在的EventLoop线程
    void assertInLoopThread() const;

private:
    static const int kInitEventListSize = 16;

    // 实际更新epoll监听状态的函数
    void update(int operation, Channel* channel);

    using ChannelMap = std::map<int, Channel*>;

    EventLoop* owner_loop_; // 所属的EventLoop
    ChannelMap channels_; // fd->Channel*的映射
    int epollfd_;
    std::vector<struct epoll_event> events_; // 用于epoll_wait返回的事件

};
