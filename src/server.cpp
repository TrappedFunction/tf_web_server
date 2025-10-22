#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h> // 定义IP地址、协议、网络接口
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <iostream>

Server::Server(uint16_t port) : port_(port){
    // 创建Socket监听
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0); // 协议：IPV4,TCP；0表示根据前面两个参数自动选择传输层协议
    if(listen_fd < 0){
        perror("socket() error");
        exit(EXIT_FAILURE);
    }

    listen_socket_ = std::make_unique<Socket>(listen_fd); // 创建私有指针 

    // 绑定地址和端口
    struct sockaddr_in serv_addr; // ipv4专用结构体，监听时需转换为sockaddr类型
    bzero(&serv_addr, sizeof(serv_addr)); // bzero将结构体所有字节清零，避免未初始化数据
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网络接口
    serv_addr.sin_port = htons(port_); // htons将主机的小端序（主机序）转换为大端序（网络序）

    if(::bind(listen_socket_->getFd(), (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        perror("bind() error");
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if(::listen(listen_socket_->getFd(), 1024) < 0){
        perror("listen() error");
        exit(EXIT_FAILURE);
    }
    std::cout << "Server listening on port " << port_ << std::endl;
}

Server::~Server() = default; // Socket的销毁由unique_ptr处理

void Server::start(){
    while(true){
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // 接受连接，client_fd是新的socket描述符，用于与此客户通信
        int client_fd = ::accept(listen_socket_->getFd(), (struct sockaddr*)&client_addr, &client_len);

        if(client_fd < 0){
            perror("accept() error");
            continue; // 继续等待下一个连接
        }

        std::cout << "New client connected, fd" << client_fd << std::endl;
        handleConnection(client_fd);
    }
}

void Server::handleConnection(int client_fd){
    // 使用RAII管理客户端连接的socket
    Socket client_socket(client_fd);
    char buffer[1024];

    while(true){
        bzero(buffer, sizeof(buffer));
        // 读取数据
        ssize_t n = ::read(client_socket.getFd(), buffer, sizeof(buffer) - 1);

        if(n > 0){
            std::cout << "Recieve from client (fd=" << client_socket.getFd()<< "): " << buffer << std::endl;
            // 写回数据
            if(::write(client_socket.getFd(), buffer, n) < 0){
                perror("write() error");
                break; // 写入失败，关闭连接
            }
        }else if(n == 0){
            // read返回0,表示客户端已关闭连接
            std::cout << "Client (fd" << client_socket.getFd() << ") disconnected." << std::endl;
            break;
        }else{
            perror("read() error");
            break;
        }
    }
    // 离开作用域，client_socket的析构函数会自动调用close(client_fd)
}