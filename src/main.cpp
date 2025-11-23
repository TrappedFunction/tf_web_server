#include "server.h"
#include "net/event_loop.h"
#include "http_utils.h"
#include "http_request.h"
#include "http_response.h"
#include "mime_types.h"
#include "net/timer.h"
#include "utils/config.h"
#include "utils/async_logging.h"
#include "utils/logger.h"
#include "http/http_router.h"
#include "http/handlers.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <list>

std::string base_path, project_root_path;
const int kIdleConnectionTimeout = 60; // 60秒空闲超时
std::unique_ptr<AsyncLogging> g_async_log;

// 全局的或由 HttpServer 类持有的 Router 对象
HttpRouter g_router;

void asyncOutput(const char* msg, int len) {
    if (g_async_log) {
        g_async_log->append(msg, len);
    }
}

// 处理http请求
void onHttpRequest(HttpRequest& req, HttpResponse* resp){
    g_router.route(req, resp);
}

// 设置给Server的MessageCallBack
void onMessage(const std::shared_ptr<Connection>& conn, Buffer* buf){
    HttpRequest& request = conn->getRequest();
    bool parse_ok = true;
    while(buf->readableBytes() > 0){
        parse_ok = request.parse(buf);
        if(request.gotAll()){
            // 活动发生，先取消旧的定时器
            TimerId old_id = conn->getTimerId();
            if (!old_id.expired()) { 
                conn->getLoop()->cancel(old_id);
            }
            HttpResponse response;
            response.addHeader("Server", "TF's Cpp Web Server");
            bool keep_alive = request.keepAlive();
            response.setKeepAlive(keep_alive);

            // 告诉客户端 Keep-Alive 的超时参数
            if (keep_alive) {
                // 告诉浏览器：建议保持55秒（比服务器实际的60秒略短，防止竞态）
                // max=10000 表示在这个连接上最多处理10000个请求
                response.addHeader("Keep-Alive", "timeout=55, max=10000");
            }

            onHttpRequest(request, &response);

            Buffer response_buf;
            response.appendToBuffer(&response_buf);
            conn->send(&response_buf);

            // Keep-alive中，将不再直接关闭
            if(keep_alive){
                std::weak_ptr<Connection> weak_conn = conn;
                TimerId new_timer_id = conn->getLoop()->runAfter(kIdleConnectionTimeout, [weak_conn](){
                    std::shared_ptr<Connection> conn_ptr = weak_conn.lock();
                    if(conn_ptr){
                        // 如果超时，服务器主动关闭连接
                        std::cout << "Connection from [" << conn_ptr->getPeerAddrStr() << "] timed out, closing." << ": fd = " << conn_ptr->getFd() << std::endl;
                        conn_ptr->forceClose(); 
                    }
                });
                conn->setTimerId(new_timer_id);
            }else{
                conn->shutdown();
            }
            request.reset();
        
        }else if (parse_ok) {
            // 数据包不完整，跳出循环，等待更多数据
            break;
        } else {
            // 解析出错
            conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
            conn->shutdown();
            break; // 出错后必须退出
        }
    }
}

// 去除首尾空白
std::string trim(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}

int main(int argc, char* argv[]){
    try{
        std::filesystem::path exe_path = std::filesystem::canonical(argv[0]);
        std::filesystem::path project_root = exe_path.parent_path().parent_path();
        project_root_path = (project_root).string();
        base_path = (project_root/"www").string();

        if(!std::filesystem::exists(base_path) || !std::filesystem::is_directory(base_path)){
            std::cerr << "Error: Web root directory '" << base_path << "' not found" << std::endl;
            return 1;
        }
        std::cout << "Using web root: " << base_path << std::endl;
    }catch(const std::filesystem::filesystem_error& e){
        std::cerr << "Filesystem error: " << e.what() << std::endl;
        return 1;
    }

    // 1. 加载配置文件
    Config config;
    std::string config_file = project_root_path + "/server.ini";
    if (argc > 1) { // 允许通过命令行参数指定配置文件
        config_file = argv[1];
    }
    if (!config.load(config_file)) {
        fprintf(stderr, "ERROR: Failed to load config file: %s\n", config_file.c_str());
        return 1;
    }

    // 初始化日志系统
    {
        std::string log_basename = config.getString("logging", "basename", "server_log");
        off_t roll_size = config.getInt("logging", "roll_size_mb", 500) * 1024 * 1024;
        int flush_interval = config.getInt("logging", "flush_interval_sec", 3);
        
        g_async_log = std::make_unique<AsyncLogging>(log_basename, roll_size, flush_interval);
        Logger::setOutput(asyncOutput);
        
        std::string log_level_str = config.getString("logging", "log_level", "INFO");
        if (log_level_str == "TRACE") Logger::setLogLevel(Logger::TRACE);
        else if (log_level_str == "DEBUG") Logger::setLogLevel(Logger::DEBUG);
        else if (log_level_str == "WARN") Logger::setLogLevel(Logger::WARN);
        else if (log_level_str == "ERROR") Logger::setLogLevel(Logger::ERROR);
        else if (log_level_str == "FATAL") Logger::setLogLevel(Logger::FATAL);
        else Logger::setLogLevel(Logger::INFO);
        
        g_async_log->start();
    }

    try{
        // **从配置文件动态加载路由**
        LOG_INFO << "Loading routes from config...";
        const auto& routes_config = config.getSection("routes");
        if (routes_config.empty()) {
            LOG_WARN << "No [routes] section found in config file.";
        } else {
            const auto& handler_registry = Handlers::getHandlerRegistry();
            for (const auto& pair : routes_config) {
                // 解析 "METHOD, /path/pattern, handler_name"
                std::string value = pair.second;
                size_t comment_pos = value.find_first_of(";#");
                if (comment_pos != std::string::npos) {
                    value = value.substr(0, comment_pos);
                }
                std::stringstream ss(value);
                std::string method_str, path, handler_name;
                
                std::getline(ss, method_str, ',');
                std::getline(ss, path, ',');
                std::getline(ss, handler_name);

                method_str = trim(method_str);
                path = trim(path);
                handler_name = trim(handler_name);

                // 字符串转 Method 枚举
                HttpRequest::Method method = HttpRequest::INVALID;
                if (method_str == "GET") method = HttpRequest::GET;
                else if (method_str == "POST") method = HttpRequest::POST;
                // ...

                // 查找 Handler
                auto handler_it = handler_registry.find(handler_name);
                
                if (method != HttpRequest::INVALID && handler_it != handler_registry.end()) {
                    if (g_router.addRoute(method, path, handler_it->second)) {
                        LOG_INFO << "Added route: " << method_str << " " << path << " -> " << handler_name;
                    }
                } else {
                    LOG_ERROR << "Failed to add route: " << pair.second;
                }
            }
        }
        g_router.addRoute(HttpRequest::GET, "/.well-known/appspecific/com.chrome.devtools.json", 
        [](const HttpRequest&, HttpResponse* resp){
            resp->setStatusCode(HttpResponse::k404NotFound);
            resp->setContentLength(0);
        });



        EventLoop loop;
        int num_threads = config.getInt("server", "threads", 0);

        // ----------------HTTP Server----------------------------------
        uint16_t http_port = config.getInt("server", "http_port", 8080);
        Server http_server(&loop, http_port, kIdleConnectionTimeout, num_threads);
        http_server.setMessageCallback(onMessage); // HTTP请求的处理逻辑
        http_server.start();
        LOG_INFO << "HTTP_Server starting...";
        LOG_INFO << "Port: " << http_port;
        LOG_INFO << "Worker Threads: " << num_threads;
        LOG_INFO << "Web Root: " << base_path;

        // --------------------- HTTPS Server ------------------------------------
        std::unique_ptr<Server> https_server_ptr;
        if (config.getBool("server", "enable_ssl", false)) {
            uint16_t https_port = config.getInt("server", "https_port", 8443);
            https_server_ptr = std::make_unique<Server>(&loop, https_port, kIdleConnectionTimeout, num_threads);
            https_server_ptr->setMessageCallback(onMessage); // 同一个处理逻辑

            std::string cert_path = project_root_path + "/" + config.getString("ssl", "cert_path");
            std::string key_path = project_root_path + "/" + config.getString("ssl", "key_path");
            https_server_ptr->enableSsl(cert_path, key_path);

            https_server_ptr->start();

            LOG_INFO << "HTTPS_Server starting...";
            LOG_INFO << "Port: " << https_port;
            LOG_INFO << "Worker Threads: " << num_threads;
            LOG_INFO << "Web Root: " << base_path;
        }
        // 启动事件循环
        loop.loop();
        
        g_async_log.release(); // 释放所有权
    }catch(const std::exception& e){
        // 异常处理代码
        LOG_FATAL << "Exception caught in main: " << e.what();
        return 1;
    }
    return 0;
}