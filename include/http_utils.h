#pragma once
#include <string>
#include <filesystem>
#include <optional>

namespace HttpUtils{
    // 根据基目录验证并解析所请求的路径
    // 有效则返回完整的安全路径，否则返回std::nullopt
    std::optional<std::string> getSafeFilePath(const std::string& base_path, const std::string& req_path);
}