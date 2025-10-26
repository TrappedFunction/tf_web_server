#pragma once
#include "connection.h"
#include <memory> // 内存管理工具，包括智能指针
#include <functional>
#include <map>



class Server{
public:
    using ConnectionPtr = std::shared_ptr<Connection>;
    // 定义回调函数类型
    using ConnectionCallback = std::function<void(const std::shared_ptr<Connection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<Connection>&, Buffer*)>;

    explicit Server(uint16_t port);
    ~Server();
    void start();

    // 设置回调
    void setConnectionCallback(const ConnectionCallback& cb) { connection_callback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { message_callback_ = cb; }
private:
    // 处理新的连接的建立
    void handleConnection();
    // 连接关闭时，由此函数进行清理
    void removeConnection(const ConnectionPtr& conn);

    std::unique_ptr<Socket> listen_socket_;
    uint16_t port_;
    // 回调函数
    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;

    // 管理所有连接，key是sockfd
    std::map<int, ConnectionPtr> connections_;
};