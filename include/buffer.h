#pragma once
#include <string>
#include <vector>
#include <algorithm>

class Buffer{
public:
    Buffer(size_t initial_size = 1024);

    // 从fd读取数据到缓冲区
    ssize_t readFd(int fd, int* saved_errno);

    // 可读字节数
    size_t readableBytes() const;

    // 获取可读数据的指针
    const char* peek() const;

    // 取出len字节的数据
    void retrieve(size_t len);
    void retrieveUntil(const char* end);
    void retrieveAll();
    std::string retrieveAllAsString();
private:
    char* begin();
    const char* begin() const;

    std::vector<char> buffer_;
    size_t reader_index_;

};