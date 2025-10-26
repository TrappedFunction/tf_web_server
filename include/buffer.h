#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>

class Buffer{
public:
    static const size_t kCheapPrepend = 8; // 缓冲区预留前置区域，用于在数据前添加内容且不需要移动现有数据
    static const size_t kInitialSize = 1024;


    explicit Buffer(size_t initial_size = kInitialSize)
        : buffer_(kCheapPrepend + initial_size),
          reader_index_(kCheapPrepend),
          write_index_(kCheapPrepend) {}
    
    // 获取可读数据的指针
    const char* peek() const { return begin() + reader_index_; }

    // 可读字节数
    size_t readableBytes() const { return write_index_ - reader_index_; }

    // 可写字节数
    size_t writableBytes() const { return buffer_.size() - write_index_; }

    // 可使用前置字节数
    size_t prependableBytes() const { return reader_index_; }

    // 回收len字节的数据
    void retrieve(size_t len){
        assert(len <= readableBytes());
        if(len < readableBytes()) {
            reader_index_ += len;
        } else {
            retrieveAll();
        }
    }
    void retrieveUntil(const char* end){
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(end - peek());
    }
    void retrieveAll(){
        reader_index_ = kCheapPrepend;
        write_index_ = kCheapPrepend;
    }
    std::string retrieveAllAsString(){
        return retrieveAsString(readableBytes());
    }
    std::string retrieveAsString(size_t len){
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    // 向缓冲区写数据
    void append(const char* data, size_t len){
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        hasWritten(len);
    }

    void append(const std::string& str){
        append(str.data(), str.length());
    }


    // 从fd读取数据到缓冲区
    ssize_t readFd(int fd, int* saved_errno);

private:
    // 提供只读和可修改重载
    char* begin() { return &*buffer_.begin(); }
    const char* begin() const { return &*buffer_.begin(); }
    char* beginWrite() { return begin() + write_index_; }
    const char* beginWrite() const { return begin() + write_index_; }

    void hasWritten(size_t len) { write_index_ += len; }

    void ensureWritableBytes(size_t len){
        if(writableBytes() < len){
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }

    // 空间扩容
    void makeSpace(size_t len);

    std::vector<char> buffer_;
    size_t reader_index_;
    size_t write_index_;

};