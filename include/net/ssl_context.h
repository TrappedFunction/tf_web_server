#pragma once
#include <openssl/ssl.h> // 用于配置ssl/tls的全局上下文
#include <string>

class SslContext{
public:
    SslContext(const std::string& cert_path, const std::string& key_path);
    ~SslContext();

    SSL_CTX* get() const { return ctx_; }
private:
    SSL_CTX* ctx_;
};