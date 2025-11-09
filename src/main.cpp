#include "server.h"
#include "net/event_loop.h"
#include "http_utils.h"
#include "http_request.h"
#include "http_response.h"
#include "mime_types.h"
#include <iostream>
#include <filesystem>
#include <fstream>

std::string base_path;
const int kIdleConnectionTimeout = 60; // 60秒空闲超时

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
    HttpRequest request;
    // 缓冲区的数据可能不足以解析一个完整的请求
    if(!request.parse(buf)){
        // 如果不足，等待下一次数据到来，出错发出400响应
        if(!request.getMethod() == HttpRequest::INVALID){
            conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
            // conn->shutdown(); // 关闭连接
        }
        return;
    }
    if(request.gotAll()){
        HttpResponse response;
        response.addHeader("Server", "TF's Cpp Web Server");
        bool keep_alive = request.keepAlive();
        response.setKeepAlive(keep_alive);

        onHttpRequest(request, &response);

        Buffer response_buf;
        response.appendToBuffer(&response_buf);
        conn->send(&response_buf);

        // HTTP/1.0短连接
        // Keep-alive中，将不再直接关闭
        if(keep_alive){
            std::weak_ptr<Connection> weak_conn = conn;
            conn->getLoop()->runAfter(kIdleConnectionTimeout, [weak_conn](){
                std::shared_ptr<Connection> conn = weak_conn.lock();
                if(conn){
                    // 如果超时，服务器主动关闭连接
                    conn->shutdown();
                }
            });
        }else{
            conn->shutdown();
        }
        
    }
}

int main(int argc, char* argv[]){
    if(argc != 2){
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    try{
        std::filesystem::path exe_path = std::filesystem::canonical(argv[0]);
        std::filesystem::path project_root = exe_path.parent_path().parent_path();
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

    try{
        EventLoop loop;
        uint16_t port = std::stoi(argv[1]);
        Server my_server(&loop, port);

        // my_server.setConnectionCallback(onConnection);
        my_server.setMessageCallback(onMessage);
        
        my_server.start();
        // 启动事件循环
        loop.loop();
    }catch(const std::exception& e){
        // 异常处理代码
        std::cerr << "Exception caught in main: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}