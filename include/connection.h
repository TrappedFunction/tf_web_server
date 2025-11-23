#pragma once
#include "net/event_loop.h"
#include "net/channel.h"
#include "socket.h"
#include "buffer.h"
#include "utils/timestamp.h"
#include "http_request.h"
#include "net/timer.h"
#include <memory>
#include <functional>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <any> // cpp17 用于存储定时器上下文, 类型安全的方式持有任何类型的值

class Server;

// 对象由share_ptr管理
class Connection : public std::enable_shared_from_this<Connection>{
public:
    enum StateE { kConnecting, kConnected, kDisconnecting, kDisconnected };

    using ConnectionPtr = std::shared_ptr<Connection>;
    using ConnectionCallback = std::function<void(const ConnectionPtr&)>;
    using MessageCallback = std::function<void(const ConnectionPtr&, Buffer*)>;
    using closeCallback = std::function<void(const ConnectionPtr&)>;

    // ssl为nullptr则为普通HTTP连接
    Connection(EventLoop* loop, int sockfd, const struct sockaddr_in& peer_addr, SSL* ssl);
    ~Connection();
    
    void send(const std::string& msg);
    void send(Buffer* buf);

    // 设置回调函数
    void setConnectionCallback(const ConnectionCallback& cb) { connection_callback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { message_callback_ = cb; }
    void setCloseCallback(const closeCallback& cb) { close_callback_ = cb; }

    // 当建立连接时由Server调用
    void connectionEstablished();
    void setupHttpContext();

    // 获取对端地址的公共方法
    std::string getPeerAddrStr() const;

    // 关闭连接的公共接口
    void shutdown();

    // 强制关闭接口
    void forceClose(); 

    // 设置当前连接的状态
    void setState(StateE s) { state_ = s; }

    // 让Server可以获得Channel
    Channel* getChannel() const { return channel_.get(); }
    int getFd() const { return socket_->getFd(); }
    EventLoop* getLoop() const { return loop_; }

    // 直接使用具体的类型，更清晰
    void setTimerId(TimerId id) { timer_id_ = id; }
    TimerId getTimerId() const { return timer_id_; }

    // 用于超时管理的方法
    void updateLastActiveTime() { last_active_time_ = Timestamp::now(); }
    Timestamp getLastActiveTime() const { return last_active_time_; }

    HttpRequest& getRequest() { return request_; }
private:
    // 在Server主循环中被调用，处理读事件
    void handleRead();
    // 在Server的主循环被调用，处理写事件
    void handleWrite();
    // 处理关闭事件
    void handleClose();
    // 处理错误事件
    void handleError();

    void sendInLoop(const std::string& msg);
    void shutdownInLoop();
    void forceCloseInLoop(); 

    // SSL握手逻辑
    void handleHandShake();

    // 一个私有函数，用于在连接真正建立后（HTTP）或握手成功后（HTTPS）进行通用设置
    void onConnectionEstablished();

    EventLoop* loop_; // 每个Connection都知道自己的EventLoop
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_; // 每个connection拥有一个Channel
    Buffer input_buffer_;
    Buffer output_buffer_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    closeCallback close_callback_;

    struct sockaddr_in peer_addr_;
    StateE state_;
    TimerId timer_id_;
    
    Timestamp last_active_time_;

    std::unique_ptr<SSL, decltype(&SSL_free)> ssl_;
    enum class SslState { kHandshaking, kEstablished, kClosing};
    SslState ssl_state_;
    HttpRequest request_; 
};