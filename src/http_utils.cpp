#include "http_utils.h"
#include <string>
#include <filesystem>
#include <optional>

namespace HttpUtils {
std::optional<std::string> getSafeFilePath(const std::string& base_path, const std::string& req_path) {
    // 1. URL解码 (简化)
    std::string decoded_path = req_path; // 假设 HttpRequest 已经解码

    // 2. 拒绝包含 ".."
    if (decoded_path.find("..") != std::string::npos) {
        return std::nullopt;
    }

    // 3. 规范化路径并检查
    try {
        std::filesystem::path full_path = std::filesystem::weakly_canonical(base_path + decoded_path);
        
        if (full_path.string().rfind(std::filesystem::weakly_canonical(base_path).string(), 0) != 0) {
            return std::nullopt;
        }
        
        if (!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)) {
            return std::nullopt;
        }

        return full_path.string();

    } catch (const std::filesystem::filesystem_error&) {
        return std::nullopt;
    }
}
}
