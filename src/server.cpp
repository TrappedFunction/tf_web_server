#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h> // 定义IP地址、协议、网络接口
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

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
        handleConnection();
    }
}

void Server::handleConnection(){
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = ::accept(listen_socket_->getFd(), (struct sockaddr*)&client_addr, &client_len);
    if(client_fd < 0){
        perror("accept error");
        return;
    }

    std::cout << "Accepted new connection from client, fd=" << client_fd << std::endl;

    // 创建一个新的Connection对象来管理这个连接
    ConnectionPtr conn = std::make_shared<Connection>(this, client_fd, client_addr);

    // 设置回调函数
    conn->setConnectionCallback(connection_callback_);
    conn->setMessageCallback(message_callback_);
    conn->setCloseCallback(std::bind(&Server::removeConnection, this, std::placeholders::_1));

    // 将新的连接加入map管理
    connections_[client_fd] = conn;

    // 触发连接建立回调
    conn->connectionEstablished();

    // 在阻塞模型下，必须处理完整个连接
    // 循环处理该连接的读事件，直到关闭
    while(connections_.count(client_fd)){
        conn->handleRead();
    }
}

void Server::removeConnection(const ConnectionPtr& conn){
    if(conn){
        int fd = conn->socket_->getFd();
        if(connections_.count(fd)){
            connections_.erase(fd);
            std::cout << "Removed connection fd=" << fd << "from server." << std::endl;
        }
    }
}

// void Server::httpHandler(const HttpRequest& req, HttpResponse& resp){
    
//     std::string req_path = req.getPath();
//     // 安全检测：req_path路径是否包含“..”
//     if(req_path.find("..") != std::string::npos){
//         // 恶意请求，返回400 Bad Request
//         // TODO ...
//         return;
//     }
//     extern std::string base_path;
//     //安全检测：规范化路径并检测是否仍在根目录下
//     std::filesystem::path full_path = std::filesystem::weakly_canonical(base_path + req_path);
//     if(full_path.string().rfind(std::filesystem::weakly_canonical(base_path).string(), 0) != 0){
//         // 试图访问根目录之外的文件，返回402 Forbidden
//         // TODO...
//         return;
//     }

//     std::string file_path = full_path.string();
//     // TODO根据请求的路径，决定如何填充response
//     if(req.getMethod() != HttpRequest::GET){
//         resp.setStatusCode(HttpResponse::k404NotFound);
//         resp.setStatusMessage("Not Found");
//         resp.setBody("<html><body><h1>404 Not Found</h1></body></html>");
//         resp.setContentType("text/html");
//         return;
//     }

//     std::ifstream file(file_path);
//     if(file.is_open()){
//         std::stringstream ss;
//         ss << file.rdbuf();
//         resp.setStatusCode(HttpResponse::k2000k);
//         resp.setStatusMessage("OK");
//         resp.setBody(ss.str());
//         // TODO简单的MIME类型判断
//         if(file_path.find(".html") != std::string::npos){
//             resp.setContentType("text/html");
//         }else if(file_path.find(".txt") != std::string::npos){
//             resp.setContentType("text/plain"); // 标准的纯文本文件
//         }else{
//             resp.setContentType("application/octet-stream"); // 二进制流数据，文件传输异常或服务器配置特殊下载方式
//         }
//     }else{
//         resp.setStatusCode(HttpResponse::k404NotFound);
//         resp.setStatusMessage("Not Found");
//         resp.setBody("<html><body><h1>404 Not Found</h1><h2>File not found at" + req.getPath() + "</h2></body></html>");
//         resp.setContentType("text/html");
//     }
// }