#include "http_request.h"
#include "http_response.h"
#include "http/handlers.h"
#include "http_utils.h" // For getSafeFilePath
#include "utils/logger.h"
#include "mime_types.h"
#include <filesystem>
#include <fstream>
#include <sstream>

// 外部变量，由 main.cpp 初始化
extern std::string base_path;

namespace Handlers {

// 全局 Handler 注册表实例
HandlerRegistry& getHandlerRegistry() {
    static HandlerRegistry registry;
    return registry;
}

// 自动注册辅助类的构造函数
HandlerRegistrar::HandlerRegistrar(const std::string& name, HttpHandler handler) {
    getHandlerRegistry()[name] = handler;
}

// ------------------------- 具体的 Handler 实现 -----------------------------

// 处理登录请求 (POST /login)
void handleLogin(const HttpRequest& req, HttpResponse* resp) {
    std::string username = req.getPostValue("username");
    std::string password = req.getPostValue("password");
    LOG_INFO << "Login attempt: username=" << username;

    if (username == "admin" && password == "123456") {
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setContentType("text/html; charset=utf-8");
        resp->setBody("<html><body><h1>Login Successful!</h1></body></html>");
    } else {
        resp->setStatusCode(HttpResponse::k403Forbidden);
        resp->setContentType("text/html; charset=utf-8");
        resp->setBody("<html><body><h1>Login Failed!</h1></body></html>");
    }
    resp->setContentLength(resp->getBody().length());
}
REGISTER_HANDLER("login", handleLogin);

// 处理获取用户信息的请求 (GET /user_info)
void handleGetUserById(const HttpRequest& req, HttpResponse* resp) {
    const auto& params = req.getRouteParams();
    if (params.empty()) {
        // 应该不会发生，但做好防御性编程
        resp->setStatusCode(HttpResponse::k400BadRequest);
        resp->setBody("Missing user ID");
        return;
    }
    std::string user_id = params[0]; // 第一个捕获组
    LOG_INFO << "Get user info for ID: " << user_id;
    
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody("{\"user_id\": " + user_id + ", \"name\": \"User from DB\"}");
    resp->setContentLength(resp->getBody().length());
}
REGISTER_HANDLER("getUserById", handleGetUserById);

void handleGetProductByName(const HttpRequest& req, HttpResponse* resp) {
    const auto& params = req.getRouteParams();
    if (params.empty()) { /* ... */ return; }
    std::string product_name = params[0];
    LOG_INFO << "Get product info for name: " << product_name;

    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody("{\"product_name\": \"" + product_name + "\", \"price\": 99.9}");
    resp->setContentLength(resp->getBody().length());
}
REGISTER_HANDLER("getProductByName", handleGetProductByName);

// 处理静态文件请求 (通配)
void handleStaticFile(const HttpRequest& req, HttpResponse* resp) {
    std::string path = req.getPath();
    if (path == "/") {
        path = "/index.html";
    }

    auto safe_path_opt = HttpUtils::getSafeFilePath(base_path, path);

    if (!safe_path_opt) {
        // 如果静态文件找不到，我们也可以调用 handleNotFound
        // 但这里为了清晰，直接构建404响应
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setContentType("text/html; charset=utf-8");
        resp->setBody("<html><body><h1>404 Not Found</h1><p>Static file not found.</p></body></html>");
        resp->setContentLength(resp->getBody().length());
        return;
    }
    
    std::string file_path = *safe_path_opt;

    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    if(file){
        // 使用stringstream读取整个文件内容
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setStatusMessage("OK");
        // 使用MimeType类设置正确的Content-Type
        std::filesystem::path fs_path(file_path);
        resp->setContentType(MimeTypes::getMimeType(fs_path.extension().string()));
        resp->setBody(buffer.str());
        resp->setContentLength(resp->getBody().length());
    }else{
        // 文件存在但是存在读取错误
        resp->setStatusCode(HttpResponse::k500InternalServerError);
        resp->setStatusMessage("Internal Server Error");
        resp->setContentType("text/html; charset=utf-8"); // 简化处理
        resp->setBody("<html><body><h1>500 Internal Server Error</h1></body></html>");
        resp->setContentLength(resp->getBody().length());
    }
}
REGISTER_HANDLER("static", handleStaticFile); // 自动注册
} // namespace Handlers