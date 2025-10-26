#pragma once
#include "buffer.h"
#include <string>
#include <unordered_map>


class HttpResponse{
public:
    enum HttpStatusCode{
        kUnknow,
        k2000k = 200,
        k400BadRequest = 400,
        k403Forbidden = 403,
        k404NotFound = 404,
        k500InternalServerError = 500,
    };

    explicit HttpResponse();
    ~HttpResponse() = default;

    void setStatusCode(HttpStatusCode code) {status_code_ = code; }
    void setStatusMessage(const std::string& message) {status_message_ = message; }
    void setContentType(const std::string& content_type) {addHeader("Content-Type", content_type); }
    void addHeader(const std::string& key, const std::string& value) {headers_[key] = value; }
    void setBody(const std::string& body) {body_ = body; }
    // 添加Content-Length头
    void setContentLength(int len) { addHeader("Content-Length", std::to_string(len)); }
    // 添加Connection头为Keep-Alive做准备
    void setKeepAlive(bool on){
        if(on) addHeader("Connection", "Keep-Alive");
        else addHeader("Connection", "Close");
    }
    HttpStatusCode getStatusCode() const { return status_code_; }
    std::string getStatusMessage() const { return status_message_; } 
    std::string getBody() const { return body_; }
    
    // 将HTTP响应报文写入Buffer, 实现字符串拼接，状态行\r\n，头部：值\r\n，\r\n，正文的格式
    void appendToBuffer(Buffer* buffer) const;
private:
    HttpStatusCode status_code_;
    std::string status_message_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    void addStatusLineToBuffer(Buffer* buffer) const;
    void addHeadersToBuffer(Buffer* buffer) const;
};