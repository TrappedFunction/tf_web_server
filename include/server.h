#pragma once
#include "socket.h"
#include <memory> // 内存管理工具，包括智能指针
class Server{
public:
    explicit Server(uint16_t port);
    ~Server();
    void start();
private:
    void handleConnection(int client_fd);
    std::unique_ptr<Socket> listen_socket_;
    uint16_t port_;
};