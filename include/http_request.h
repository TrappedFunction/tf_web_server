#pragma once
#include <string>
#include <unordered_map>

class HttpRequest{
public:
    enum Method {GET, POST, HEAD, PUT, DELETE, INVALID};

    HttpRequest();

    // 简化版解析函数，直接收取字符串
    bool parse(const std::string& request_str);

    Method getMethod() const {return method_; }
    const std::string& getPath() const {return path_; }

    void reset();

private:
    bool parseRequestLine(const std::string& line);

    Method method_;
    std::string path_;
    std::string version_;
    //暂时不解析Headers和Body
};