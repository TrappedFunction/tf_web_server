#pragma once
#include "http_request.h"
#include "http_response.h"
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <regex>

// 所有HTTP请求处理函数的统一签名
// 参数：解析好的请求对象，待填充的响应对象
using HttpHandler = std::function<void(const HttpRequest&, HttpResponse*)>;

class HttpRouter{
public:

    // 路由规则结构体
    struct Route {
        HttpRequest::Method method;
        std::regex path_regex;
        HttpHandler handler;
    };

    // 添加一个路由规则
    // @param method: HTTP方法
    // @param path_pattern: 包含正则表达式的URL模式
    // @param handler: 处理函数
    // @return: 如果正则表达式编译成功，返回 true
    bool addRoute(HttpRequest::Method method, const std::string& path_pattern, HttpHandler handler);

    // 根据请求进行路由分发
    // @param req: 客户端请求
    // @param resp: 待填充的响应
    void route(HttpRequest& req, HttpResponse* resp) const;

private:
    // 404 Not Found 的默认处理函数
    void handleNotFound(const HttpRequest& req, HttpResponse* resp) const;
    
    // 精确匹配：map<path, map<method, handler>>
    std::map<std::string, std::map<HttpRequest::Method, HttpHandler>> static_routes_;
    // 正则匹配：vector<Route>
    std::vector<Route> regex_routes_;
};