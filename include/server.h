#pragma once
#include "connection.h"
#include "net/event_loop.h"
#include "net/channel.h"
#include "socket.h"
#include <memory> // 内存管理工具，包括智能指针
#include <functional>
#include <map>

class EventLoopThreadPool;

class Server{
public:
    using ConnectionPtr = std::shared_ptr<Connection>;
    // 定义回调函数类型
    using ConnectionCallback = std::function<void(const std::shared_ptr<Connection>&)>;
    using MessageCallback = std::function<void(const std::shared_ptr<Connection>&, Buffer*)>;

    explicit Server(EventLoop* loop, uint16_t port, const int kIdleConnectionTimeout, int num_threads = 0);
    ~Server();

    // 启动非阻塞服务器
    void start();

    // 设置回调
    void setConnectionCallback(const ConnectionCallback& cb) { connection_callback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { message_callback_ = cb; }
    void onConnection(const ConnectionPtr& conn);
private:
    // 处理新的连接的建立
    void handleConnection();
    // 连接关闭时，由此函数进行清理
    void removeConnection(const ConnectionPtr& conn);
    // 在循环内部移除，避免迭代器失效问题
    void removeConnectionInLoop(const ConnectionPtr& conn);

    EventLoop* loop_;
    const uint16_t port_;

    std::unique_ptr<Socket> listen_socket_;
    std::unique_ptr<Channel> accept_channel_; // 用于监听新连接的Channel
    
    // 回调函数
    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;

    // 管理所有连接，key是sockfd
    std::map<int, ConnectionPtr> connections_;
    const int kIdleConnectionTimeout; // 60秒空闲超时

    // 线程池成员
    std::unique_ptr<EventLoopThreadPool> thread_pool_;
};