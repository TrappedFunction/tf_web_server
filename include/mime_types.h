#pragma once
#include <string>
#include <unordered_map>

// 增加MIME类型映射
class MimeTypes{
public:
    static std::string getMimeType(const std::string& extension);
private:
    static const std::unordered_map<std::string, std::string> mime_map_;
};