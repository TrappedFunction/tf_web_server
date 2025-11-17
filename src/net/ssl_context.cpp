#include "net/ssl_context.h"
#include <openssl/err.h>
#include <stdexcept>

SslContext::SslContext(const std::string& cert_path, const std::string& key_path){
    // 创建SSL_CTX
    ctx_ = SSL_CTX_new(TLS_server_method());
    if(!ctx_){
        throw std::runtime_error("SSL_CTX_new failed");
    }

    // 加载服务器证书
    if(SSL_CTX_use_certificate_file(ctx_, cert_path.c_str(), SSL_FILETYPE_PEM) <= 0){
        ERR_print_errors_fp(stderr);
        throw std::runtime_error("SSL_CTX_use_certificate_file failed");
    }

    // 加载服务器私钥
    if(SSL_CTX_use_PrivateKey_file(ctx_, key_path.c_str(), SSL_FILETYPE_PEM) <= 0){
        ERR_print_errors_fp(stderr);
        throw std::runtime_error("SSL_CTX_use_PrivateKey_file failed");
    }

    // 验证私钥和证书是否匹配
    if(!SSL_CTX_check_private_key(ctx_)){
        ERR_print_errors_fp(stderr);
        throw std::runtime_error("Private key does not match the certificate");
    }
}

SslContext::~SslContext(){
    if(ctx_){
        SSL_CTX_free(ctx_);
    }
}