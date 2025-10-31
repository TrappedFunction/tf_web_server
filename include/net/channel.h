#pragma once
#include <functional>
#include <memory>

class EventLoop;

// Channel不拥有文件描述符，它的生命周期由Connection等对象管理
class Channel{
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();
    
    // 核心：事件处理函数，由EventLoop调用
    void handleEvent();

    // 设置回调函数
    void setReadCallback(const EventCallback& cb) { read_callback_ = cb; }
    void setWriteCallback(const EventCallback& cb) { write_callback_ = cb; }
    void setCloseCallback(const EventCallback& cb) { close_callback_ = cb; }
    void setErrorCallback(const EventCallback& cb) { error_callback_ = cb; }

    int getFd() const { return fd_; }
    int getEvents() const {return events_; }
    void set_revents(int revt) { revents_ = revt; } // 由Poller调用

    bool isNoneEvent() const { return events_ == kNoneEvent; }

    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    EventLoop* ownerLoop() { return loop_; }
    
private:
    void update();

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_; // 所属的EventLoop
    const int fd_;
    int events_; // 关注的事件
    int revents_; // 实际发生的事件

    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;
};