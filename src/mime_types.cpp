#include "mime_types.h"

const std::unordered_map<std::string, std::string> MimeTypes::mime_map_ = {
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".txt", "text/plain"},
    {".png", "image/png"},
    {".jpg", "image/jpg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".mp3", "text/html; charset=utf-8"},
    {".mp4", "text/html; charset=utf-8"},
    {".pdf", "text/html; charset=utf-8"},
};

std::string MimeTypes::getMimeType(const std::string& extension){
    auto it = mime_map_.find(extension);
    if(it != mime_map_.end()){
        return it->second;
    }
    // 如果找不到，返回通用二进制流类型
    return "application/octet-stream";
}