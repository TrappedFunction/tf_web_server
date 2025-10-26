#include "http_response.h"
#include <cstdio>

const std::unordered_map<int, std::string> kStatusCodeMessages = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {500, "Internal Server Error"}
};

HttpResponse::HttpResponse() : status_code_(kUnknow){

}
void HttpResponse::appendToBuffer(Buffer* buffer) const{
    char buf[128];

    // 添加状态行(Status Line)
    auto it = kStatusCodeMessages.find(status_code_);
    std::string status_message = (it == kStatusCodeMessages.end()) ? "Unknown" : it->second;
    // 如果用户设置了自定义消息，则使用用户的
    if(!status_message_.empty()){
        status_message = status_message_;
    }

    snprintf(buf, sizeof(buf), "HTTP/1.1 %d %s\r\n", status_code_, status_message.c_str());
    buffer->append(buf);

    // 添加所有头部Headers
    for(const auto& header : headers_){
        buffer->append(header.first);
        buffer->append(": ");
        buffer->append(header.second);
        buffer->append("\r\n");
    }

    // 添加一个空行，分隔头部和正文
    buffer->append("\r\n");

    // 添加正文body
    if(!body_.empty()){
        buffer->append(body_);
    }
}