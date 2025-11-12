#include "server.h"
#include "net/timer.h"
#include <netinet/in.h> // 定义IP地址、协议、网络接口
#include <iostream>
#include <string>
#include <strings.h>

Server::Server(EventLoop* loop, uint16_t port) : loop_(loop), port_(port),
    listen_socket_(new Socket(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))),
    accept_channel_(new Channel(loop, listen_socket_->getFd()))
{
    // 创建Socket监听
    listen_socket_->bindAddress(port_);
    // 设置accept_channel_的读回调为handleConnection
    accept_channel_->setReadCallback(std::bind(&Server::handleConnection, this));
    
}

Server::~Server() = default; // Socket的销毁由unique_ptr处理

void Server::start(){
    listen_socket_->listen();
    // accept_channel_注册到EventLoop中，开始监听新连接事件
    accept_channel_->enableReading();
    setConnectionCallback(std::bind(&Server::onConnection, this, std::placeholders::_1));
    std::cout << "Server starts listening on port " << port_ << std::endl;
}

void Server::handleConnection(){
    loop_->assertInLoopThread();
    // 循环accept，因为ET模式可能一次性有多个连接到达
    while(true){
        // 可能一次到达 多个连接，所以声明和初始化需在循环中进行
        struct sockaddr_in peer_addr;
        // bzero(&peer_addr, sizeof(peer_addr));
        socklen_t addr_len = sizeof(peer_addr);
        int connfd = listen_socket_->accept(&peer_addr, &addr_len);
        if(connfd >= 0){
            std::cout << "Accepted new connection from client, fd=" << connfd << std::endl;

            // 创建一个新的Connection对象来管理这个连接
            ConnectionPtr conn = std::make_shared<Connection>(loop_, connfd, peer_addr);

            // 设置回调函数
            conn->setConnectionCallback(connection_callback_);
            conn->setMessageCallback(message_callback_);
            conn->setCloseCallback(std::bind(&Server::removeConnection, this, std::placeholders::_1));
            // 将新的连接加入map管理
            connections_[connfd] = conn;
            // 触发连接建立回调
            conn->connectionEstablished();
        }else{
            // 在非阻塞模式下，阿accept返回-1且errno为EAGAIN表示所有新连接都已处理完毕
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }else{
                // Log SYSERR
                break;
            }
        }
    }
}

void Server::removeConnection(const ConnectionPtr& conn){
    // TODO 跨线程调用时，需要将任务放入目标loop的队列中执行，暂不实现
    
    // 从Eentloop中移除Channel
    // conn->getChannel()->disableAll();
    // conn->getChannel()->remove();
    loop_->runInLoop(std::bind(&Server::removeConnectionInLoop, this, conn));
}
void Server::removeConnectionInLoop(const ConnectionPtr& conn){
    loop_->assertInLoopThread();
    int fd = conn->getFd();
    size_t n = connections_.erase(fd);
    assert(n == 1);

    // 此时从Channel触发的事件已经处理完毕，可以安全移除Channel
    EventLoop* io_loop = conn->getLoop();
    io_loop->removeChannel(conn->getChannel());
}

// 当新连接建立或断开时调用
void Server::onConnection(const ConnectionPtr& conn){
    // 根据连接状态进行判断
    std::cout << "New connection from [" << conn->getPeerAddrStr() << "]" << std::endl;
    // 为新连接设置初始的超时定时器
    std::weak_ptr<Connection> weak_conn = conn;
    TimerId timer_id = conn->getLoop()->runAfter(kIdleConnectionTimeout, [weak_conn](){
        std::shared_ptr<Connection> conn_ptr = weak_conn.lock();
        if(conn_ptr){
            std::cout << "Connection from [" << conn_ptr->getPeerAddrStr() << "] timed out, closing." << std::endl;
            conn_ptr->shutdown();
        }
    });
    // 将TimerId存入Connection的上下文
    conn->setContext(timer_id);
}
