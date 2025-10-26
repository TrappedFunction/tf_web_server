#include "connection.h"
#include "server.h"
#include <unistd.h>
#include <iostream>
#include <cerrno>
#include <arpa/inet.h>

Connection::Connection(Server* server, int sockfd, const struct sockaddr_in& peer_addr) 
  : server_(server), 
    socket_(std::make_unique<Socket>(sockfd)), 
    peer_addr_(peer_addr){

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
    if(connection_callback_){
        // 使用shared_from_this()获取自身的shared_ptr
        connection_callback_(shared_from_this());
    }
}

void Connection::send(const std::string& msg){
    ::write(socket_->getFd(), msg.c_str(), msg.length());
    // TODO在阻塞IO模型下，假设write一次性能写完
    // 非阻塞模型需要将数据加入output_buffer_，并注册可写事件
}

void Connection::send(Buffer* buf){
    ::write(socket_->getFd(), buf->peek(), buf->readableBytes());
}

void Connection::handleRead(){
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
    // TODO 暂不实现
}

void Connection::handleClose(){
    std::cout << "Client fd=" << socket_->getFd() << "closed connection." << std::endl;
    // 调用上层设置的close callback，通知Server移除这个连接
    if(close_callback_){
        close_callback_(shared_from_this());
    }
}

void Connection::handleError(){
    // 错误处理逻辑，与关闭类似
    handleClose();
}

void Connection::shutdown(){
    handleClose();
}