#pragma once // 头文件被多次包含只处理一次
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h> // unix的标准头文件，包括文件处理、进程处理
// socket文件描述符是唯一资源，不能被两个不同对象指向，需禁止被复制
class NonCopyable{
public:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

class Socket : NonCopyable{
public:
    explicit Socket(int fd); // explicit用于禁止隐式类型转换
    ~Socket(); // 销毁时关闭fd
    int getFd() const { return fd_;}; // 不允许修改该函数内的成员变量，除非成员变量用mutable修饰

    // 设置非阻塞和close-on-exec
    void setNonBlockAndCloseExec();
    // 封装bind，listen，accept
    void bindAddress(uint16_t port);
    void listen();
    // accept返回的int fd和对端地址
    int accept(struct sockaddr_in* peer_addr, socklen_t* addr_len);
    // 关闭连接的写半边
    void shutdownWrite();
private:
    int fd_;
};