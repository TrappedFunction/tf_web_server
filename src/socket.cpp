#include "socket.h"
#include "utils/logger.h"
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <stdlib.h>
#include <sys/socket.h>

Socket::Socket(int fd) : fd_(fd){
    if (fd_ < 0) {
        perror("socket creation failed");
        exit(1);
    }

    // **新增：启用 SO_REUSEADDR**
    int optval = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        // 这是一个非致命错误，可以选择不退出，但最好打日志
    }
}
Socket::~Socket(){
    if(fd_ > 0){
        ::close(fd_); // ::表示调用的函数close()属于全局函数，而非成员函数
    }
}

void Socket::setNonBlockAndCloseExec(){
    // non-block
    int flags = ::fcntl(fd_, F_GETFL, 0);
    flags |= O_NONBLOCK;
    ::fcntl(fd_, F_SETFL, flags);

    // close-on-exec
    flags = ::fcntl(fd_, F_GETFD, 0);
    flags |= O_CLOEXEC;
    ::fcntl(fd_, F_GETFD, flags);
}

void Socket::bindAddress(uint16_t port){
    // 绑定地址和端口
    struct sockaddr_in serv_addr; // ipv4专用结构体，监听时需转换为sockaddr类型
    bzero(&serv_addr, sizeof(serv_addr)); // bzero将结构体所有字节清零，避免未初始化数据
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网络接口
    serv_addr.sin_port = htons(port); // htons将主机的小端序（主机序）转换为大端序（网络序）

    if(::bind(fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        perror("bind() error");
        exit(1);
    }
}

void Socket::listen(){
    // 开始监听
    if(::listen(fd_, SOMAXCONN) < 0){
        LOG_ERROR <<"listen() error";
        exit(EXIT_FAILURE);
    }
}

int Socket::accept(struct sockaddr_in* peer_addr, socklen_t* addr_len){
    int client_fd = ::accept4(fd_, (struct sockaddr*)peer_addr, addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if(client_fd < 0){
        // TODO 错误处理，非阻塞模式下EAGAIN或EWOULDBLOCK是正常情况
    }
    return client_fd;
}

void Socket::shutdownWrite(){
    // 调用系统调用shutdown，并指定SHUT_WR
    if(::shutdown(fd_, SHUT_WR) < 0){ // 向对端发送一个FIN包，告知数据已写完，但会继续接收数据
        // Log SYSERR "Socket::shutdownWrite"
        perror("Socket::shutdownWrite() error");
    }
}