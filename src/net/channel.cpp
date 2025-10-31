#include "net/channel.h"
#include "net/event_loop.h"
#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0) {}

Channel::~Channel(){}

void Channel::handleEvent(){
    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)){
        if(close_callback_) close_callback_();
    }

    if(revents_ & (EPOLLERR)){
        if(error_callback_) error_callback_();
    }

    if(revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)){
        if(read_callback_) read_callback_();
    }

    if(events_ & EPOLLOUT){
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