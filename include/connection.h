#pragma once
#include <memory>
#include <functional>
#include "socket.h"
#include "buffer.h"
#include <netinet/in.h>

class Server;

// 对象由share_ptr管理
class Connection : public std::enable_shared_from_this<Connection>{
public:
    using ConnectionPtr = std::shared_ptr<Connection>;
    using ConnectionCallback = std::function<void(const ConnectionPtr&)>;
    using MessageCallback = std::function<void(const ConnectionPtr&, Buffer*)>;
    using closeCallback = std::function<void(const ConnectionPtr&)>;

    Connection(Server* server, int sockfd, const struct sockaddr_in& peer_addr);
    ~Connection();
    
    void send(const std::string& msg);
    void send(Buffer* buf);

    // 设置回调函数
    void setConnectionCallback(const ConnectionCallback& cb) { connection_callback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { message_callback_ = cb; }
    void setCloseCallback(const closeCallback& cb) { close_callback_ = cb; }

    // 当建立连接时由Server调用
    void connectionEstablished();

    // 获取对端地址的公共方法
    std::string getPeerAddrStr() const;

    // 关闭连接的公共接口
    void shutdown();
private:
    // 在Server主循环中被调用，处理读事件
    void handleRead();
    // TODO 在Server的主循环被调用，处理写事件，用于高并发模型，暂不实现
    void handleWrite();
    // 处理关闭事件
    void handleClose();
    // 处理错误事件
    void handleError();
    // 允许Server访问私有成员
    friend class Server;

    Server* server_; // 指向所属的Server
    std::unique_ptr<Socket> socket_;
    Buffer input_buffer_;
    Buffer output_buffer_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    closeCallback close_callback_;

    struct sockaddr_in peer_addr_;
};