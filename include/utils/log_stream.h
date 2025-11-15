#pragma once
#include <string>
#include <cstring>
#include <algorithm>
#include <thread>
#include <sstream>

const int kSmallBuffer = 4000;
const int kLargeBuffer = 4000 * 4000;

// 模仿std::ostream，将各种类型的数据格式化到一块内存缓冲区中
template<int SIZE>
class FixedBuffer{
public:
    FixedBuffer() : cur_(data_) {}
    ~FixedBuffer() {}
    // append, length, data, reset ...
    void append(const char* buf, size_t len){
        if(avail() > len){
            memcpy(cur_, buf, len);
            cur_ += len;
        }
    }
    const char* data() const { return data_; }
    int length() const { return static_cast<int>(cur_ - data_); }
    char* current() { return cur_; }
    size_t avail() const { return static_cast<size_t>(end() - cur_); }
    void add(size_t len) { cur_+= len; }
    void reset() { cur_ = data_; }
    void bzero() { memset(data_, 0, sizeof(data_)); }
private:
    const char* end() const { return data_ + sizeof(data_); }
    char data_[SIZE];
    char* cur_;
};

// 将各种类型转换为字符串append到FixedBuffer中
class LogStream{
public:
    using Buffer = FixedBuffer<kSmallBuffer>;
    LogStream& operator<<(bool v) {
        buffer_.append(v ? "1" : "0", 1);
        return *this;
    }
    LogStream& operator<<(short);
    LogStream& operator<<(unsigned short);
    LogStream& operator<<(int);
    LogStream& operator<<(unsigned int);
    LogStream& operator<<(long);
    LogStream& operator<<(unsigned long);
    LogStream& operator<<(long long);
    LogStream& operator<<(unsigned long long);
    LogStream& operator<<(const void*);
    LogStream& operator<<(float v) {
        *this << static_cast<double>(v);
        return *this;
    }
    LogStream& operator<<(double);
    LogStream& operator<<(char v) {
        buffer_.append(&v, 1);
        return *this;
    }
    LogStream& operator<<(const char* str) {
        if (str) {
            buffer_.append(str, strlen(str));
        } else {
            buffer_.append("(null)", 6);
        }
        return *this;
    }
    LogStream& operator<<(const std::string& v) {
        buffer_.append(v.c_str(), v.size());
        return *this;
    }

    LogStream& operator<<(const std::thread::id& tid) {
        std::stringstream ss;
        ss << tid;
        buffer_.append(ss.str().c_str(), ss.str().length());
        return *this;
    }

    void  append(const char* data, int len) { buffer_.append(data, len); }
    const Buffer& buffer() const { return buffer_; }
    void resetBuffer() { buffer_.reset(); }
private:
    template<typename T> void formatInteger(T);
    Buffer buffer_;
    static const int kMaxNumericSize = 32;
};

