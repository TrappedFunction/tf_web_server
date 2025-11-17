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
#include <iostream>
#include <filesystem>
#include <fstream>
#include <list>

std::string base_path, project_root_path;
const int kIdleConnectionTimeout = 60; // 60秒空闲超时
std::unique_ptr<AsyncLogging> g_async_log;

void asyncOutput(const char* msg, int len) {
    if (g_async_log) {
        g_async_log->append(msg, len);
    }
}

// 处理http请求
void onHttpRequest(const HttpRequest& req, HttpResponse* resp){
    std::string req_path = req.getPath();
    if(req_path == "/"){
        req_path = "/index.html";
    }

    auto safe_path_opt = HttpUtils::getSafeFilePath(base_path, req_path);

    if(!safe_path_opt){
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setStatusMessage("Not Found");
        resp->setContentType("text/html; charset=utf-8");
        resp->setBody("<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested URL was not found on this server.</p></body></html>");
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

        resp->setStatusCode(HttpResponse::k2000k);
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

// 设置给Server的MessageCallBack
void onMessage(const std::shared_ptr<Connection>& conn, Buffer* buf){
    HttpRequest& request = conn->getRequest();
    bool parse_ok = true;
    while(buf->readableBytes() > 0){
        parse_ok = request.parse(buf);
        if(request.gotAll()){
            // 活动发生，先取消旧的定时器
            if(conn->getContext().has_value()){
                TimerId timer_id = std::any_cast<TimerId>(conn->getContext());
                conn->getLoop()->cancel(timer_id);
            }
            HttpResponse response;
            response.addHeader("Server", "TF's Cpp Web Server");
            bool keep_alive = request.keepAlive();
            response.setKeepAlive(keep_alive);

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
                        std::cout << "Connection from [" << conn_ptr->getPeerAddrStr() << "] timed out, closing." << std::endl;
                        conn_ptr->shutdown();
                    }
                });
                conn->setContext(new_timer_id);
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
        std::cerr << "Exception caught in main: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}