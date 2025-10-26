#pragma once
#include <string>
#include <filesystem>
#include <optional>

namespace HttpUtils{
    // 根据基目录验证并解析所请求的路径
    // 有效则返回完整的安全路径，否则返回std::nullopt
    std::optional<std::string> getSafeFilePath(const std::string& base_path, const std::string& req_path){
        // URL解码
        // TODO 暂不实现URL解码
        std::string decoded_path = req_path;

        // 拒绝包含".."的路径
        if(decoded_path.find("..") != std::string::npos){
            return std::nullopt;
        }

        // 规范化路径并进行根目录检查
        try{
            std::filesystem::path full_path = std::filesystem::weakly_canonical(base_path + decoded_path);

            // 检测解析后的路径是否仍在base_path下
            if(full_path.string().find(std::filesystem::weakly_canonical(base_path).string()) != 0){
                return std::nullopt;
            }

            // 确保文件存在且是常规文件(防止请求目录)
            if(!std::filesystem::exists(full_path) || !std::filesystem::is_regular_file(full_path)){
                return std::nullopt;
            }
            return full_path.string();
        }catch(const std::filesystem::filesystem_error&){
            return std::nullopt;
        }
    }
}