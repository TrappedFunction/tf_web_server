#include "http/http_router.h"
#include "utils/logger.h" // 用于日志

bool HttpRouter::addRoute(HttpRequest::Method method, const std::string& path_pattern, HttpHandler handler) {
    // 简单的判断：如果路径中没有特殊字符，认为是静态路由
    if (path_pattern.find_first_of("*+?()[]{}|^$") == std::string::npos) {
        static_routes_[path_pattern][method] = handler;
        LOG_INFO << "Adding regex route: " << path_pattern; // **添加这行日志**
        return true;
    } else {
        try {
            regex_routes_.push_back({method, std::regex(path_pattern), handler});
            return true;
        } catch (const std::regex_error& e) {
            LOG_ERROR << "Invalid regex pattern '" << path_pattern << "': " << e.what();
            return false;
        }
    }
}

void HttpRouter::route(HttpRequest& req, HttpResponse* resp) const {
    // 1. 优先尝试精确匹配，性能更高
    auto path_it = static_routes_.find(req.getPath());
    if (path_it != static_routes_.end()) {
        auto method_it = path_it->second.find(req.getMethod());
        if (method_it != path_it->second.end()) {
            method_it->second(req, resp);
            return;
        }
    }

    // 2. 如果精确匹配失败，再尝试正则表达式匹配
    std::smatch match;
    for (const auto& route : regex_routes_) {
        if (req.getMethod() == route.method && std::regex_match(req.getPath(), match, route.path_regex)) {
            // 匹配成功
            RouteParams params;
            // match[0] 是整个匹配的字符串，我们从 match[1] 开始提取捕获组
            for (size_t i = 1; i < match.size(); ++i) {
                params.push_back(match[i].str());
            }
            // 将捕获的参数存入 HttpRequest 对象
            req.setRouteParams(params);
            
            route.handler(req, resp);
            return;
        }
    }

    // 3. 所有匹配都失败，返回 404
    handleNotFound(req, resp);
}

void HttpRouter::handleNotFound(const HttpRequest& req, HttpResponse* resp) const {
    LOG_WARN << "No route found for " << (req.getMethod() == HttpRequest::GET ? "GET" : "POST") 
             << " " << req.getPath();
    
    resp->setStatusCode(HttpResponse::k404NotFound);
    resp->setContentType("text/html; charset=utf-8");
    resp->setBody("<html><body><h1>404 Not Found</h1></body></html>");
    resp->setContentLength(resp->getBody().length());
}