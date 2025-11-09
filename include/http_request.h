#pragma once
#include "buffer.h"
#include <string>
#include <unordered_map>

class HttpRequest{
public:
    enum Method {GET, POST, HEAD, PUT, DELETE, INVALID};
    enum ParseState{
        kExpectRequestLine,
        kExpectHeaders,
        kExpectBody,
        kGotALL,
    };

    HttpRequest();

    // 简化版解析函数，直接收取字符串
    bool parse(Buffer* buffer);
    bool gotAll() const { return state_ == kGotALL; };

    Method getMethod() const {return method_; }
    const std::string& getPath() const {return path_; }
    std::string getHeader(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& getHeaders() const { return headers_; }

    void reset();
    bool keepAlive() const;

private:
    bool parseRequestLine(const char* begin, const char* end);
    bool parseHeader(const char* begin, const char* end);

    ParseState state_;
    Method method_;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
};