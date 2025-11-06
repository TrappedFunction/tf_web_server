#include "connection.h"
#include <iostream>
#include <cerrno>
#include <arpa/inet.h>

Connection::Connection(EventLoop* loop, int sockfd, const struct sockaddr_in& peer_addr) 
  : loop_(loop), 
    socket_(std::make_unique<Socket>(sockfd)), 
    channel_(std::make_unique<Channel>(loop, sockfd)),
    peer_addr_(peer_addr),
    state_(kConnecting){
        
}

Connection::~Connection(){
    std::cout << "Connection fd=" << socket_->getFd() << " destroyed. " << std::endl;
}

std::string Connection::getPeerAddrStr() const {
    char ip_str[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &peer_addr_.sin_addr, ip_str, sizeof(ip_str));
    uint16_t port = ::ntohs(peer_addr_.sin_port);
    return std::string(ip_str) + ":" + std::to_string(port);
}

void Connection::connectionEstablished(){
    loop_->assertInLoopThread();
    // 开始监听可读事件
    setState(kConnected);

    // 不直接绑定this，而是绑定一个weak_ptr
        std::weak_ptr<Connection> weak_self = shared_from_this();
        channel_->setReadCallback([weak_self]() {
            // 在执行回调时，尝试将weak_ptr提升为shared_ptr
            std::shared_ptr<Connection> guard_ptr = weak_self.lock();
            if(guard_ptr){
                // 如果提升成功，说明对象活着，调用成员函数
                guard_ptr->handleRead();
            }
        });
        channel_->setWriteCallback([weak_self]() {
            // 在执行回调时，尝试将weak_ptr提升为shared_ptr
            std::shared_ptr<Connection> guard_ptr = weak_self.lock();
            if(guard_ptr){
                // 如果提升成功，说明对象活着，调用成员函数
                guard_ptr->handleWrite();
            }
        });
        channel_->setCloseCallback([weak_self]() {
            // 在执行回调时，尝试将weak_ptr提升为shared_ptr
            std::shared_ptr<Connection> guard_ptr = weak_self.lock();
            if(guard_ptr){
                // 如果提升成功，说明对象活着，调用成员函数
                guard_ptr->handleClose();
            }
        });
        channel_->setErrorCallback([weak_self]() {
            // 在执行回调时，尝试将weak_ptr提升为shared_ptr
            std::shared_ptr<Connection> guard_ptr = weak_self.lock();
            if(guard_ptr){
                // 如果提升成功，说明对象活着，调用成员函数
                guard_ptr->handleError();
            }
        });

    // 将Channel和Connection自己绑定在一起
    channel_->tie(shared_from_this());
    channel_->enableReading();
    // 调用应用层设置的onConnection回调
    connection_callback_(shared_from_this());
}

void Connection::send(const std::string& msg){
    // 跨线程调用暂不实现
    sendInLoop(msg);
}

void Connection::sendInLoop(const std::string& msg){
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = msg.length();
    bool fault_error = false;

    // 如果输出缓冲区为空，尝试直接发送
    if(output_buffer_.readableBytes() == 0){
        nwrote = ::write(socket_->getFd(), msg.c_str(), msg.length());
        if(nwrote >= 0){
            remaining = msg.length() - nwrote;
            if(remaining == 0){
                // 全部发送完毕
                return;
            }
        }else{
            nwrote = 0;
            if(errno != EWOULDBLOCK){
                // Log SYSERR
                fault_error = true;
            }
        }
    }

    // 如果没有出错，并且还有数据没发完
    if(!fault_error && remaining > 0){
        // 将剩余数据放入输出缓存区
        output_buffer_.append(msg.substr(nwrote));
        // 开始监听可写事件
        if(!channel_->isWriting()){
            channel_->enableWriting();
        }
    }
}

void Connection::send(Buffer* buf){
    ::write(socket_->getFd(), buf->peek(), buf->readableBytes());
}

void Connection::handleRead(){
    loop_->assertInLoopThread();
    int saved_errno = 0;
    ssize_t n = input_buffer_.readFd(socket_->getFd(), &saved_errno);

    if(n > 0){
        // 成功读到了数据，调用上层设置的message callback
        if(message_callback_){
            message_callback_(shared_from_this(), &input_buffer_);
        }
    }else if(n == 0){
        // 客户端关闭连接
        handleClose();
    }else{
        // 读取错误
        errno = saved_errno;
        std::cerr << "Connection::handleRead error" << std::endl;
        handleError();
    }
}


void Connection::handleWrite(){
    loop_->assertInLoopThread();
    if(channel_->isWriting()){
        ssize_t n = ::write(socket_->getFd(), output_buffer_.peek(), output_buffer_.readableBytes());
        if(n > 0){
            output_buffer_.retrieve(n);
            if(output_buffer_.readableBytes() == 0){
                // 数据发送完毕，必须停止监听可写事件，否则会busy-loop
                channel_->disableWriting();
                // 如果此时有关闭连接的计划，可以在这里执行
            }
        }else{
            // Log SYSERR
        }
    }
}

void Connection::handleClose(){
    loop_->assertInLoopThread();
    if(state_ == kConnected || state_ == kDisconnecting){
        setState(kDisconnected);
        channel_->disableAll();
        ConnectionPtr guard_this(shared_from_this());
        // 仍然需要通知上层连接已关闭
        std::cout << "Connection from [" << getPeerAddrStr() << "] is closing." << std::endl;

        close_callback_(guard_this);
        // loop_->removeChannel(channel_.get());
    }
    
    // std::cout << "Client fd=" << socket_->getFd() << "closed connection." << std::endl;
}

void Connection::handleError(){
    // 错误处理逻辑，与关闭类似
    handleClose();
}

void Connection::shutdown(){
    shutdownInLoop();
}

void Connection::shutdownInLoop(){
    loop_->assertInLoopThread();
    if(state_ == kConnected){
        setState(kDisconnecting); // 进入正在断开连接的状态
        if(!channel_->isWriting()){
            socket_->shutdownWrite(); // 关闭写半边
        }
    }
    
}