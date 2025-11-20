#include "http_request.h"
#include <algorithm>
#include <sstream>
#include <iostream>

HttpRequest::HttpRequest(){
    reset();
}

void HttpRequest::reset(){
    method_ = INVALID;
    state_ = kExpectRequestLine;
    path_ = "";
    query_ = "";
    version_ = "";
    headers_.clear();
    body_ = "";
    post_params_.clear();
}

// URL解码实现
std::string HttpRequest::urlDecode(const std::string& str) {
    std::string result;
    char hex[3] = {0};
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                hex[0] = str[i+1];
                hex[1] = str[i+2];
                result += static_cast<char>(strtol(hex, nullptr, 16));
                i += 2;
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

const char* searchCRLF(const char* begin, const char* end){
    const char* crlf = std::search(begin, end, "\r\n", "\r\n"+2);
    return crlf == end ? nullptr : crlf;
}

bool HttpRequest::parse(Buffer* buffer){
    bool ok = true;
    bool has_more = true;
    while(has_more){
        const char* start = buffer->peek();
        const char* end = start + buffer->readableBytes();
        if(state_ == kExpectRequestLine){
            const char* crlf = searchCRLF(start, end);
            if(crlf){
                ok = parseRequestLine(start, crlf);
                if(ok){
                    buffer->retrieveUntil(crlf+2);
                    state_ = kExpectHeaders;
                }else{
                    has_more = false;
                }
            }else{
                has_more = false;
            }
        }else if(state_ == kExpectHeaders){
            const char* crlf = searchCRLF(start, end);
            if(crlf){
                if(start == crlf){
                    // 解析Header
                    buffer->retrieveUntil(crlf+2);
                    // TODO简化处理，假设GET/HEAD请求没有Body
                    // 需根据Content-Length读取Body
                    std::string content_length_str = getHeader("Content-Length");
                    if(!content_length_str.empty() && method_ == POST){
                        int content_length = std::stoi(content_length_str);
                        if(content_length > 0){
                            state_ = kExpectBody;
                        }else{
                            state_ = kGotALL;
                            has_more = false;
                        }
                        
                    }else{
                        state_ = kGotALL;
                        has_more = false;
                    }
                }else{
                    ok = parseHeader(start, crlf);
                    if(ok){
                        buffer->retrieveUntil(crlf+2);
                    }else{
                        has_more = false;
                    }
                }
            }else{
                has_more = false;
            }
        }else if(state_ == kExpectBody){
            parseBody(buffer);
            state_ = kGotALL;
            has_more = false;
        }
    }
    return ok;
}

void HttpRequest::parseBody(Buffer* buffer) {
    int content_length = std::stoi(getHeader("Content-Length"));
    if (buffer->readableBytes() >= content_length) {
        body_ = buffer->retrieveAsString(content_length);
        state_ = kGotALL;
        // 如果是 POST 表单，解析它
        if (getHeader("Content-Type") == "application/x-www-form-urlencoded") {
            parsePost();
        }
    }
}

// 解析 POST 表单数据
void HttpRequest::parsePost() {
    std::string& data = body_;
    std::string key, value;
    size_t start = 0, end;
    
    while(start < data.length()){
        end = data.find('=', start);
        if(end == std::string::npos) break;
        key = urlDecode(data.substr(start, end - start));
        
        start = end + 1;
        end = data.find('&', start);
        if(end == std::string::npos){
            end = data.length();
        }
        value = urlDecode(data.substr(start, end - start));
        
        post_params_[key] = value;
        start = end + 1;
    }
}

std::string HttpRequest::getPostValue(const std::string& key) const {
    auto it = post_params_.find(key);
    return (it == post_params_.end()) ? "" : it->second;
}

bool HttpRequest::parseRequestLine(const char* begin, const char* end){
    std::string line(begin, end);
    size_t method_end = line.find(' ');
    if(method_end == std::string::npos) return false;
    std::string method_str = line.substr(0, method_end);

    if(method_str == "GET") method_ = GET;
    else if(method_str == "POST") method_ = POST;
    else if(method_str == "HEAD") method_ = HEAD;
    else if(method_str == "PUT") method_ = PUT;
    else if(method_str == "DELETE") method_ = DELETE;
    else method_ = INVALID;

    if(method_ == INVALID) return false;

    // 解析 Path 和 Query
    size_t path_start = line.find(' ');
    size_t path_end = line.rfind(' ');
    if (path_start == std::string::npos || path_end == std::string::npos || path_start == path_end) {
        return false;
    }

    std::string url = line.substr(path_start + 1, path_end - (path_start + 1));
    size_t query_pos = url.find('?');
    if (query_pos != std::string::npos) {
        path_ = urlDecode(url.substr(0, query_pos));
        query_ = urlDecode(url.substr(query_pos + 1));
    } else {
        path_ = urlDecode(url);
    }

    // 处理根路径
    if(path_ == "/"){
        path_ = "/index.html";
    }

    return true;
}

// 实现完整的key-value提取
bool HttpRequest::parseHeader(const char* begin, const char* end){
    const char* colon = std::find(begin, end, ':');
    if(colon == end) return false;

    std::string key(begin, colon);
    // 跳过':'以及任何空格
    const char* value_start = colon+1;
    while(value_start < end && isspace(*value_start)){
        value_start++;
    }

    std::string value(value_start, end);

    headers_[key] = value;
    return true;
}

std::string HttpRequest::getHeader(const std::string& key) const{
    auto it = headers_.find(key);
    return it == headers_.end() ? "" : it->second;
}

bool HttpRequest::keepAlive() const {
    std::string connection = getHeader("Connection");
    if(connection == "close"){
        return false;
    }
    if(version_ == "HTTP/1.0"){
        return connection == "Keep-Alive";
    }
    // 对于HTTP/1.1默认Keep-Alive
    return true;
}