#include "server.h"
#include "net/timer.h"
#include "net/event_loop_thread_pool.h"
#include "net/ssl_context.h"
#include <netinet/in.h> // 定义IP地址、协议、网络接口
#include <iostream>
#include <string>
#include <strings.h>
#include <openssl/err.h>

Server::Server(EventLoop* loop, uint16_t port, const int kIdleConnectionTimeout, int num_threads) : loop_(loop), port_(port),
    listen_socket_(new Socket(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0))),
    accept_channel_(new Channel(loop, listen_socket_->getFd())), kIdleConnectionTimeout(kIdleConnectionTimeout),
    thread_pool_(new EventLoopThreadPool(loop, "worker", num_threads))
{
    // 创建Socket监听
    listen_socket_->bindAddress(port_);
    // 设置accept_channel_的读回调为handleConnection
    accept_channel_->setReadCallback(std::bind(&Server::handleConnection, this));
    
}

Server::~Server() {
    loop_->assertInLoopThread();
    std::cout << "Server destructing, stop listening on port " << port_ << std::endl;
    
    // **在析构前，必须将 accept_channel_ 从 EventLoop 中移除**
    accept_channel_->disableAll(); // 停止监听所有事件
    accept_channel_->remove();     // 从 Poller 中移除
}

void Server::start(){
    thread_pool_->start(); // 启动线程池
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
        bzero(&peer_addr, sizeof(peer_addr));
        socklen_t addr_len = sizeof(peer_addr);
        int connfd = listen_socket_->accept(&peer_addr, &addr_len);
        if(connfd >= 0){
            // std::cout << "Accepted new connection from client, fd=" << connfd << std::endl;

            // 从线程池中获取一个I/O loop
            EventLoop* io_loop = thread_pool_->getNextLoop();

            // 创建SSL对象
            SSL* ssl = nullptr;
            if(ssl_context_){
                ssl = SSL_new(ssl_context_->get());
                if(!ssl){
                    std::cerr << "SSL_new failed" << std::endl;
                    ::close(connfd);
                    continue;
                }
                // 将fd与SSL对象关联
                if(SSL_set_fd(ssl, connfd) == 0){
                    ERR_print_errors_fp(stderr);
                    SSL_free(ssl);
                    ::close(connfd);
                    continue;
                }
                SSL_set_accept_state(ssl);
            }
            // 在选中的I/O loop中创建和初始化Connection
            io_loop->runInLoop([this, io_loop, connfd, peer_addr, ssl](){
                // 创建一个新的Connection对象来管理这个连接
                ConnectionPtr conn = std::make_shared<Connection>(io_loop, connfd, peer_addr, ssl);

                // 设置回调函数
                conn->setConnectionCallback(connection_callback_);
                conn->setMessageCallback(message_callback_);
                conn->setCloseCallback(std::bind(&EventLoop::removeConnection, io_loop, std::placeholders::_1));
                // 在io_loop自己的线程中将新的连接加入自己的map管理
                io_loop->addConnection(connfd, conn);
                // 触发连接建立回调
                conn->connectionEstablished();
            });
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



// 当新连接建立或断开时调用
void Server::onConnection(const ConnectionPtr& conn){
    // 根据连接状态进行判断
    std::cout << "New connection from [" << conn->getPeerAddrStr() << "]" << ": fd = " << conn->getFd() << std::endl;
    // 为新连接设置初始的超时定时器
    std::weak_ptr<Connection> weak_conn = conn;
    TimerId timer_id = conn->getLoop()->runAfter(kIdleConnectionTimeout, [weak_conn](){
        std::shared_ptr<Connection> conn_ptr = weak_conn.lock();
        if(conn_ptr){
            std::cout << "Connection from [" << conn_ptr->getPeerAddrStr() << "] timed out, closing." << ": fd = " << conn_ptr->getFd() << std::endl;
            conn_ptr->forceClose(); 
        }
    });
    // 将TimerId存入Connection的上下文
    conn->setTimerId(timer_id);
}

void Server::enableSsl(const std::string& cert_path, const std::string& key_path){
    ssl_context_ = std::make_unique<SslContext>(cert_path, key_path);
}
