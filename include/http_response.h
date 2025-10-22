#pragma once
#include <string>
#include <unordered_map>

class Buffer; // 前向声明

class HttpResponse{
public:
    enum HttpStatusCode{
        kUnknow,
        k2000k = 200,
        k404NotFound = 404,
    };

    explicit HttpResponse();

    void setStatusCode(HttpStatusCode code) {status_code_ = code; }
    void setStatusMessage(const std::string& message) {status_message_ = message; }
    void setContentType(const std::string& content_type) {addHeader("Content-Type", content_type); }
    void addHeader(const std::string& key, const std::string& value) {headers_[key] = value; }
    void setBody(const std::string& body) {body_ = body; }
    HttpStatusCode getStatusCode() const { return status_code_; }
    std::string getStatusMessage() const { return status_message_; } 
    std::string getBody() const { return body_; }
    
    // 将HTTP响应报文写入Buffer, 实现字符串拼接，状态行\r\n，头部：值\r\n，\r\n，正文的格式
    void appendToBuffer(Buffer& buffer) const;
private:
    HttpStatusCode status_code_;
    std::string status_message_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};