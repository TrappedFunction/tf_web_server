#include "net/channel.h"
#include "net/event_loop.h"
#include <sys/epoll.h>
#include <cassert>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0) {}

Channel::~Channel(){
    // Channel对象被析构时，必须确保它不再监听任何事件
    assert(isNoneEvent());
}

void Channel::handleEvent(){
    // 事件处理前，检查被绑定的对象是否还存活
    if(tied_){
        std::shared_ptr<void> guard = tie_.lock();
        if(guard){
            // 对象还活着
            handleEventWithGuard();
        }
        // 如果guard为空，则对象已销毁，不作处理
    }else{
        handleEventWithGuard();
    }
}

void Channel::handleEventWithGuard(){
    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)){
        if(close_callback_) close_callback_();
    }

    if(revents_ & (EPOLLERR)){
        if(error_callback_) error_callback_();
    }

    if(revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)){
        if(read_callback_) read_callback_();
    }

    if(revents_ & EPOLLOUT){
        if(write_callback_) write_callback_();
    }
}

void Channel::enableReading(){
    events_ |= kReadEvent;
    update();
}

void Channel::disableReading(){
    events_ &= ~kReadEvent;
    update();
}

void Channel::enableWriting(){
    events_ |= kWriteEvent;
    update();
}

void Channel::disableWriting(){
    events_ &= ~kWriteEvent;
    update();
}

void Channel::disableAll(){
    events_ = kNoneEvent;
    update();
}

void Channel::update(){
    loop_->updateChannel(this);
}

void Channel::remove(){
    // 调用EventLoop的removeChannel来执行实际的移除操作
    loop_->removeChannel(this);
}

void Channel::tie(const std::shared_ptr<void>& obj){
    tie_ = obj;
    tied_ = true;
}