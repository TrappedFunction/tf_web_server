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
    version_ = "";
    headers_.clear();
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
                    // 如果是POST，需根据Content-Length读取Body
                    if(method_ == POST){
                        state_ = kExpectBody;
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
            // TODO暂不处理
            state_ = kGotALL;
            has_more = false;
        }
    }
    return ok;
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

    size_t path_end = line.find(' ', method_end + 1);
    if(path_end == std::string::npos) return false;

    path_ = line.substr(method_end+1, path_end - (method_end+1));
    version_ = line.substr(path_end + 1);

    // 处理根路径
    if(path_ == "/"){
        path_ = "/index.html";
    }

    return version_ == "HTTP/1.0" || version_ == "HTTP/1.1";
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