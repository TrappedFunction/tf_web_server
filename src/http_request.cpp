#include "http_request.h"
#include <sstream>

HttpRequest::HttpRequest(){
    reset();
}

void HttpRequest::reset(){
    method_ = INVALID;
    path_ = "";
    version_ = "";
}

bool HttpRequest::parse(const std::string& request_str){
    reset();
    std::string crlf = "\r\n";
    size_t request_line_end = request_str.find(crlf);
    if(request_line_end == std::string::npos){ // 未找到匹配字段
        return false; // 请求不完整
    }

    std::string request_line = request_str.substr(0, request_line_end);
    return parseRequestLine(request_line);
}

bool HttpRequest::parseRequestLine(const std::string& line){
    std::stringstream ss(line);
    std::string method_str;
    ss >> method_str >> path_ >> version_;

    if(method_str == "GET"){
        method_ = GET;
    } else{
        method_ = INVALID;
        return false;
    }

    // 简化处理，只支持GET
    if(method_ == INVALID) return false;

    // 处理根路径
    if(path_ == "/"){
        path_ = "/index.html";
    }

    return true;
}