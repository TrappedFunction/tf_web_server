#pragma once
#include "buffer.h"
#include <string>
#include <unordered_map>
#include <algorithm>

// 用于存储从URL中捕获的参数，例如 /users/123 中的 "123"
using RouteParams = std::vector<std::string>;

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
    const std::string& getQuery() const { return query_; } // 获取查询字符串
    const std::string& getVersion() const { return version_; }
    std::string getHeader(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& getHeaders() const { return headers_; }
    const std::string& getBody() const { return body_; }

    // 解析POST表单数据 (x-www-form-urlencoded)
    std::string getPostValue(const std::string& key) const;

    void reset();
    bool keepAlive() const;

    const RouteParams& getRouteParams() const { return route_params_; }
    void setRouteParams(const RouteParams& params) { route_params_ = params; }
    // URL 解码辅助函数
    static std::string urlDecode(const std::string& str);

private:

    bool parseRequestLine(const char* begin, const char* end);
    bool parseHeader(const char* begin, const char* end);
    void parseBody(Buffer* buffer);

    // 解析表单数据
    void parsePost();

    ParseState state_;
    Method method_;
    std::string path_;
    std::string version_;
    std::string query_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;

    // 用于存储POST解析后的键值对
    std::unordered_map<std::string, std::string> post_params_;

    RouteParams route_params_;
};