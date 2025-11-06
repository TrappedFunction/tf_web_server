#include "net/poller.h"
#include "net/channel.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <cassert>
#include <cstring>

Poller::Poller(EventLoop* loop)
    : owner_loop_(loop),
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize){
        if(epollfd_ < 0){
            // log FATAL
            exit(1);
        }
}

Poller::~Poller(){
    ::close(epollfd_);
}

void Poller::assertInLoopThread() const {
    owner_loop_->assertInLoopThread();
}

void Poller::poll(int timeout_ms, ChannelList* active_channels){
    assertInLoopThread();
    int num_events = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeout_ms);
    int saved_errno = errno;

    if(num_events > 0){
        for(int i = 0; i < num_events; i++){
            Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
            // 利用epoll_event.data.ptr可以直接存储指针
            channel->set_revents(events_[i].events);
            active_channels->push_back(channel);
        }
        // 如果事件数组满了，进行扩容
        if(num_events == static_cast<int>(events_.size())){
            events_.resize(events_.size() * 2);
        }
    }else if(num_events == 0){
        // a timeout occurred
    }else{
        if(saved_errno != EINTR){
            // 忽略中断错误
            errno = saved_errno;
            // TODO Log SYSERR
        }
    }
}

void Poller::updateChannel(Channel* channel){
    assertInLoopThread();
    const int fd = channel->getFd();
    if(channels_.find(fd) == channels_.end()){
        // 新增channel
        assert(channels_.find(fd) == channels_.end());
        channels_[fd] = channel;
        update(EPOLL_CTL_ADD, channel);
    }else{
        // 更新已有channel
        assert(channels_.find(fd) != channels_.end());
        assert(channels_[fd] == channel);
        update(EPOLL_CTL_MOD, channel);
    }
}

void Poller::removeChannel(Channel* channel){
    assertInLoopThread();
    const int fd = channel->getFd();
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);

    size_t n = channels_.erase(fd);
    assert(n == 1);

    if(channel->isNoneEvent()){
        // 如果channel已经没有监听事件，epoll_ctl(DEL)会失败
        // TODO可以在这里直接返回，或者依赖update的逻辑
        // return;
    }
    update(EPOLL_CTL_DEL, channel);
}

void Poller::update(int operation, Channel* channel){
    struct epoll_event event;
    bzero(&event, sizeof(event));
    event.events = channel->getEvents();
    event.data.ptr = channel; // 将Channel指针存入，方便poll返回时直接获取
    int fd = channel->getFd();

    if(::epoll_ctl(epollfd_, operation, fd, &event) < 0){
        // TODO Log SYSERR
    }

}