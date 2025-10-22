#pragma once // 头文件被多次包含只处理一次

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
private:
    int fd_;
};