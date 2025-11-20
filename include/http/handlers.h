#pragma once
#include "http/http_router.h" // 包含 HttpHandler 定义
#include <map>
#include <string>

// Handlers 命名空间，用于组织所有处理函数
namespace Handlers {

// 静态文件处理器
void handleStaticFile(const HttpRequest& req, HttpResponse* resp);

// 登录处理器
void handleLogin(const HttpRequest& req, HttpResponse* resp);

// 根据ID获取用户处理器
void handleGetUserById(const HttpRequest& req, HttpResponse* resp);

// 根据名称获取产品处理器
void handleGetProductByName(const HttpRequest& req, HttpResponse* resp);

// ... 其他处理函数声明 ...


// TODO可以创建一个 Handler 工厂或注册表来管理这些函数
// 这使得 main 函数可以根据配置文件中的字符串名字来查找函数
using HandlerRegistry = std::map<std::string, HttpHandler>;

// 获取全局的 Handler 注册表
HandlerRegistry& getHandlerRegistry();

// 辅助类，用于在程序启动时自动注册 Handlers
class HandlerRegistrar {
public:
    HandlerRegistrar(const std::string& name, HttpHandler handler);
};

} // namespace Handlers

// 用于自动注册的宏
#define REGISTER_HANDLER(name, func) \
    static Handlers::HandlerRegistrar registrar_##func(name, Handlers::func)
