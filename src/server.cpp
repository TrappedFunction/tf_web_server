#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h> // 定义IP地址、协议、网络接口
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "buffer.h"
#include <fstream>
#include <sstream>

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

void Server::httpHandler(const HttpRequest& req, HttpResponse& resp){
    std::string file_path = "../www" + req.getPath();

    // TODO根据请求的路径，决定如何填充response
    if(req.getMethod() != HttpRequest::GET){
        resp.setStatusCode(HttpResponse::k404NotFound);
        resp.setStatusMessage("Not Found");
        resp.setBody("<html><body><h1>404 Not Found</h1></body></html>");
        resp.setContentType("text/html");
        return;
    }

    std::ifstream file(file_path);
    if(file.is_open()){
        std::stringstream ss;
        ss << file.rdbuf();
        resp.setStatusCode(HttpResponse::k2000k);
        resp.setStatusMessage("OK");
        resp.setBody(ss.str());
        // TODO简单的MIME类型判断
        if(file_path.find(".html") != std::string::npos){
            resp.setContentType("text/html");
        }else if(file_path.find(".txt") != std::string::npos){
            resp.setContentType("text/plain"); // 标准的纯文本文件
        }else{
            resp.setContentType("application/octet-stream"); // 二进制流数据，文件传输异常或服务器配置特殊下载方式
        }
    }else{
        resp.setStatusCode(HttpResponse::k404NotFound);
        resp.setStatusMessage("Not Found");
        resp.setBody("<html><body><h1>404 Not Found</h1><h2>File not found at" + req.getPath() + "</h2></body></html>");
        resp.setContentType("text/html");
    }
}

void Server::handleConnection(int client_fd){
    // 使用RAII管理客户端连接的socket
    Socket client_socket(client_fd);
    char read_buffer[1024];
    bzero(read_buffer, sizeof(read_buffer));

    // TODO在阻塞模式下，我们假设一次read就能读取完一个完整的HTTP请求
    ssize_t n = ::read(client_socket.getFd(), read_buffer, sizeof(read_buffer)-1);
    if(n <= 0){
        if(n < 0) perror("read() error");
        return; // 出错或客户端关闭连接
    }

    HttpRequest request;
    if(!request.parse(std::string(read_buffer))){
        // 解析失败，发送404响应
        // TODO简化处理，直接关闭连接
        std::cerr << "Failed to parse request" << std::endl;
        return;
    }

    HttpResponse response;
    httpHandler(request,response);

    // 将response转换为字符串发送
    // TODO直接使用拼接字符串，不使用Buffer类，简化当前阶段操作
    std::string response_str = "HTTP/1.1 " + std::to_string(response.getStatusCode()) + " " + response.getStatusMessage() + "\r\n";
    // TODO简化操作，没有实现添加Headers的逻辑
    response_str += "Content-Length: " + std::to_string(response.getBody().length()) + "\r\n";
    response_str += "\r\n";
    response_str += response.getBody();

    ssize_t written = ::write(client_socket.getFd(), response_str.c_str(), response_str.length());
    if(written < 0){
        perror("write() error");
    }
    // 短连接，处理完一个请求就断开连接
}