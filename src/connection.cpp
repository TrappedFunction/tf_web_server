#include "connection.h"
#include "net/timer.h"
#include "utils/logger.h"
#include <iostream>
#include <cerrno>
#include <arpa/inet.h>
#include <openssl/err.h>

// SSL_free的包装，用于unique_ptr
void ssl_free_deleter(SSL* ssl){
    if(ssl){
        SSL_free(ssl);
    }
}

Connection::Connection(EventLoop* loop, int sockfd, const struct sockaddr_in& peer_addr, SSL* ssl) 
  : loop_(loop), 
    socket_(std::make_unique<Socket>(sockfd)), 
    channel_(std::make_unique<Channel>(loop, sockfd)),
    peer_addr_(peer_addr),
    state_(kConnecting),
    last_active_time_(Timestamp::now()),
    ssl_(ssl, &ssl_free_deleter),
    ssl_state_(ssl ? SslState::kHandshaking : SslState::kEstablished){ // 如果有ssl，则初始状态为握手
        
}

Connection::~Connection(){
    std::cout << "Connection fd=" << socket_->getFd() << " destroyed. " << std::endl;
    assert(!channel_->isReading() && !channel_->isWriting());
}

std::string Connection::getPeerAddrStr() const {
    char ip_str[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &peer_addr_.sin_addr, ip_str, sizeof(ip_str));
    uint16_t port = ::ntohs(peer_addr_.sin_port);
    return std::string(ip_str) + ":" + std::to_string(port);
}

void Connection::setupHttpContext() {
    loop_->assertInLoopThread();
    setState(kConnected);

    // 设置 HTTP 业务回调 (Read/Write/Close/Error)
    std::weak_ptr<Connection> weak_self = shared_from_this();
    channel_->setReadCallback([weak_self]() {
        if (auto ptr = weak_self.lock()) ptr->handleRead();
    });
    channel_->setWriteCallback([weak_self]() {
        if (auto ptr = weak_self.lock()) ptr->handleWrite();
    });
    channel_->setCloseCallback([weak_self]() {
        if (auto ptr = weak_self.lock()) ptr->handleClose();
    });
    channel_->setErrorCallback([weak_self]() {
        if (auto ptr = weak_self.lock()) ptr->handleError();
    });
    
    // 注意：这里不再调用 connection_callback_，因为它已经在连接刚建立时调用过了
}

void Connection::connectionEstablished(){
    loop_->assertInLoopThread();

    // 1. 绑定 Channel 生命周期
    channel_->tie(shared_from_this());
    channel_->enableReading(); // 开启监听

    // 无论 HTTP 还是 HTTPS，连接建立那一刻就启动定时器
    // 这会调用 Server::onConnection，从而设置初始的 60秒 超时
    if (connection_callback_) {
        connection_callback_(shared_from_this());
    }
    // 根据是否有SSL，决定使用HTTPS处理还是HTTP处理
    if(ssl_){
        // 如果是HTTPS，设置握手回调并开始握手
        std::weak_ptr<Connection> weak_self = shared_from_this();
        auto handshake_cb = [weak_self]() {
            if (auto ptr = weak_self.lock()) ptr->handleHandShake();
        };
        channel_->setReadCallback(handshake_cb);
        channel_->setWriteCallback(handshake_cb);
        handleHandShake(); // 立即尝试握手
    }else{
        // http
        setupHttpContext();
    }
    
}

// 处理TLS握手
void Connection::handleHandShake(){
    loop_->assertInLoopThread();
    int ret = SSL_do_handshake(ssl_.get());

    if(ret == 1){
        // 握手成功
        ssl_state_ = SslState::kEstablished;

        // 将回调切换到正常的HTTP数据处理
        setupHttpContext();

        // 握手后可能已经有数据可读，所以立即调用read
        if (SSL_pending(ssl_.get()) > 0) {
            handleRead();
        }

    }else{
        int err = SSL_get_error(ssl_.get(), ret);
        if (err == SSL_ERROR_WANT_READ) {
            // 关键：必须确保我们正在监听读事件
            if (!channel_->isReading()) channel_->enableReading();
            // 握手期间通常不需要监听写，除非 WANT_WRITE
            if (channel_->isWriting()) channel_->disableWriting();
        } else if (err == SSL_ERROR_WANT_WRITE) {
            // 关键：必须监听写事件
            if (!channel_->isWriting()) channel_->enableWriting();
            if (channel_->isReading()) channel_->disableReading();
        } else {
            // **失败处理**
            // 打印详细错误日志
            char err_buf[256];
            ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
            LOG_ERROR << "SSL Handshake failed, fd=" << socket_->getFd() 
                      << ", SSL err=" << err << ", Detail: " << err_buf;
            
            handleError(); // 这会调用 handleClose
        }
    }
}

void Connection::send(const std::string& msg){
    if(loop_->isInLoopThread()){
        sendInLoop(msg);
    }else{
        // 跨线程发送，需要将数据和调用都转移到I/O线程
        loop_->runInLoop(std::bind(&Connection::sendInLoop,this, msg));
    }
}

void Connection::sendInLoop(const std::string& msg){
    loop_->assertInLoopThread();
    if(state_ == kDisconnected || state_ == kDisconnecting){
        LOG_WARN << "disconnected, give up writing";
        return;
    }
    ssize_t nwrote = 0;
    size_t remaining = msg.length();
    bool fault_error = false;

    // 如果输出缓冲区为空，尝试直接发送
    if(!channel_->isWriting() && output_buffer_.readableBytes() == 0){
        if(ssl_){
            nwrote = SSL_write(ssl_.get(), msg.data(), msg.length());
            if(nwrote <= 0){
                int err = SSL_get_error(ssl_.get(), nwrote);
                if(err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_WANT_READ){
                    ERR_print_errors_fp(stderr);
                    fault_error = true;
                }
                nwrote = 0;
            }
        }else{
            nwrote = ::write(socket_->getFd(), msg.c_str(), msg.length());
            
        }
        if(nwrote >= 0){
            remaining = msg.length() - nwrote;
            if(remaining == 0){
                // 全部发送完毕
                return;
            }
        }else{
            nwrote = 0;
            if(errno != EWOULDBLOCK){
                // Log SYSERR
                fault_error = true;
            }
        }
    }

    // 如果没有出错，并且还有数据没发完
    if(!fault_error && remaining > 0){
        // 将剩余数据放入输出缓存区
        output_buffer_.append(msg.substr(nwrote));
        // 开始监听可写事件
        if(!channel_->isWriting()){
            channel_->enableWriting();
        }
    }

    if(fault_error){
        handleError();
    }
}

void Connection::send(Buffer* buf){
    if(loop_->isInLoopThread()){
        sendInLoop(buf->retrieveAllAsString());
    }else{
        loop_->runInLoop(std::bind(&Connection::sendInLoop, this, buf->retrieveAllAsString()));
    }
    // ::write(socket_->getFd(), buf->peek(), buf->readableBytes());
}

void Connection::handleRead() {
    loop_->assertInLoopThread();
    int saved_errno = 0;
    // 如果已经处于断开流程，忽略数据
    if (state_ == kDisconnecting || state_ == kDisconnected) return;
    if (ssl_) { // HTTPS 逻辑
        while (true) {
            char buf[65536];
            int n = SSL_read(ssl_.get(), buf, sizeof(buf));
            if (n > 0) {
                input_buffer_.append(buf, n);
                updateLastActiveTime(); // 成功读到数据，更新时间
            } else {
                int err = SSL_get_error(ssl_.get(), n);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    break; 
                } else if (err == SSL_ERROR_ZERO_RETURN) {
                    // 收到 TLS 关闭通知
                    // 如果此时 buffer 里没有待处理数据，说明是空闲连接关闭，或者请求发送完毕后的关闭
                    if (input_buffer_.readableBytes() == 0) {
                        handleClose();
                    }
                    break; 
                } else if (err == SSL_ERROR_SYSCALL) {
                    // 系统错误，通常意味着连接重置或非正常断开
                    if (errno != 0) {
                        LOG_ERROR << "SSL_read syscall error: " << strerror(errno);
                    }else {
                        // errno == 0 表示虽然是 SYSCALL 错误，但实际上是 EOF (对端关闭了 TCP)
                        // 这在浏览器强制刷新或关闭标签页时很常见
                        LOG_INFO << "SSL_read EOF (unexpected), fd=" << socket_->getFd();
                    }
                    handleClose(); // 直接关闭
                    return;
                } else {
                    // 其他 SSL 错误
                    LOG_ERROR << "SSL_read error code: " << err;
                    handleError();
                    return;
                }
            }
        }
    } else { // HTTP 逻辑
        while (true) {
            ssize_t n = input_buffer_.readFd(socket_->getFd(), &saved_errno);
            if (n > 0) {
                // 继续
            } else if (n == 0) {
                handleClose();
                break;
            } else {
                if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {  
                    break;
                }
                handleError();
                break;
                
            }
        }
    }

    if (state_ == kDisconnecting) {
        // 既然正在断开，读到的数据没有意义了，直接忽略或强制关闭
        // 这里选择不做 callback，避免 sendInLoop 的尴尬
        return;
    }

    // 统一的后续处理
    if (state_ != kConnected) return;
    if (input_buffer_.readableBytes() > 0) {
        if (state_ == kConnected) {
            updateLastActiveTime();
            message_callback_(shared_from_this(), &input_buffer_);
        } else {
            LOG_WARN << "Received data in non-connected state";
        }
    }
}


void Connection::handleWrite(){
    loop_->assertInLoopThread();
    if(channel_->isWriting()){
        if (state_ == kDisconnecting && ssl_) {
            // **正在执行 SSL 关闭**
            int ret = SSL_shutdown(ssl_.get());
            if (ret == 1) {
                // SSL 关闭完成
                channel_->disableWriting();
                socket_->shutdownWrite(); // 最后关闭TCP写端
            } else {
                int err = SSL_get_error(ssl_.get(), ret);
                if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                    ERR_print_errors_fp(stderr);
                    handleError();
                }
                // 否则保持isWriting，等待下一次机会
            }
        }else if(ssl_){
            ssize_t n = SSL_write(ssl_.get(), output_buffer_.peek(), output_buffer_.readableBytes());
            if(n > 0){
                updateLastActiveTime();
                output_buffer_.retrieve(n);
                if(output_buffer_.readableBytes() == 0){
                    // 数据发送完毕，必须停止监听可写事件，否则会busy-loop
                    channel_->disableWriting();
                    // 如果此时有关闭连接的计划，可以在这里执行
                    if(state_ == kDisconnecting){
                        shutdownInLoop();
                    }
                }
            }else{
                int err = SSL_get_error(ssl_.get(), n);
                if(err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_WANT_READ){
                    ERR_print_errors_fp(stderr);
                    handleError();
                }
            }
        }else{
            while(true){
                size_t n = ::write(socket_->getFd(), output_buffer_.peek(), output_buffer_.readableBytes());
                if(n > 0){
                    updateLastActiveTime();
                    output_buffer_.retrieve(n);
                    if(output_buffer_.readableBytes() == 0){
                        // 数据发送完毕，必须停止监听可写事件，否则会busy-loop
                        channel_->disableWriting();
                        // 如果此时有关闭连接的计划，可以在这里执行
                        if(state_ == kDisconnecting){
                            socket_->shutdownWrite();
                        }
                        break;
                    }
                }else{
                    if(errno == EAGAIN || errno == EWOULDBLOCK){
                        // 内核缓冲区已满，不可再写
                        // 保持enableWriteing状态，等待下一次可写通知
                        std::cout << "ET mode : write buffer is full for fd=" << socket_->getFd() << std::endl;
                    }else{
                        std::cerr << "Connection::handleWrite error" << std::endl;
                        handleError();
                    }
                    break;
                }
            }
        }
    }
}

void Connection::handleClose(){
    loop_->assertInLoopThread();
    // 只要连接不是已经断开的状态，就执行关闭逻辑
    if(state_ != kDisconnected){
        setState(kDisconnected);
        channel_->disableAll();
        TimerId id = getTimerId();
        if (!id.expired()) loop_->cancel(id);
        ConnectionPtr guard_this(shared_from_this());

        close_callback_(guard_this);
    }
}

void Connection::handleError(){
    // 错误处理逻辑，与关闭类似
    handleClose();
}

void Connection::shutdown(){
    shutdownInLoop();
}

void Connection::shutdownInLoop() {
    loop_->assertInLoopThread();
    if (state_ == kConnected) {
        setState(kDisconnecting);
        if (ssl_) {
            // **HTTPS 关闭流程**
            if (!channel_->isWriting()) {
                // SSL_shutdown 可能需要多次I/O才能完成
                int ret = SSL_shutdown(ssl_.get());
                if (ret == 1) {
                    // 立即完成
                    socket_->shutdownWrite(); // 现在可以安全地关闭TCP写端了
                } else if (ret == 0) {
                    // 需要再次调用 SSL_shutdown
                    channel_->enableWriting(); // 监听写事件以便继续shutdown
                } else {
                    int err = SSL_get_error(ssl_.get(), ret);
                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                        // 出错
                        ERR_print_errors_fp(stderr);
                        handleError();
                    } else {
                        // 需要更多I/O，监听写事件
                        channel_->enableWriting();
                    }
                }
            }
        } else {
            // **HTTP 关闭流程 (保持不变)**
            if (!channel_->isWriting()) {
                socket_->shutdownWrite();
            }
        }
    }
}

void Connection::onConnectionEstablished() {
    // 这个函数包含了所有连接“就绪”后的通用逻辑
    loop_->assertInLoopThread();
    setState(kConnected);

    // 设置通用的读/写/关闭/错误回调
    std::weak_ptr<Connection> weak_self = shared_from_this();
    channel_->setReadCallback([weak_self]() {
        std::shared_ptr<Connection> guard_ptr = weak_self.lock();
        if(guard_ptr) guard_ptr->handleRead();
    });
    channel_->setWriteCallback([weak_self]() {
        std::shared_ptr<Connection> guard_ptr = weak_self.lock();
        if(guard_ptr) guard_ptr->handleWrite();
    });
    channel_->setCloseCallback([weak_self]() {
        std::shared_ptr<Connection> guard_ptr = weak_self.lock();
        if(guard_ptr) guard_ptr->handleClose();
    });
    channel_->setErrorCallback([weak_self]() {
        std::shared_ptr<Connection> guard_ptr = weak_self.lock();
        if(guard_ptr) guard_ptr->handleError();
    });

    // 将Channel和Connection自己绑定在一起
    channel_->tie(shared_from_this());
    channel_->enableReading();
    // 调用应用层设置的onConnection回调
    connection_callback_(shared_from_this());
}

void Connection::forceClose() {
    if (loop_->isInLoopThread()) {
        forceCloseInLoop();
    } else {
        loop_->queueInLoop(std::bind(&Connection::forceCloseInLoop, shared_from_this()));
    }
}

void Connection::forceCloseInLoop() {
    loop_->assertInLoopThread();
    if (state_ == kConnected || state_ == kDisconnecting) {
        handleClose(); // 直接进入关闭流程，清理资源
    }
}