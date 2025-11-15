#pragma once
#include <string>
#include <map>
#include <stdexcept>
#include "socket.h"

// A simple INI file parser
class Config : NonCopyable {
public:
    Config() = default;
    ~Config() = default;

    // 加载并解析 INI 配置文件
    // @param filename: INI 文件的路径
    // @return: 如果加载和解析成功，返回 true，否则返回 false
    bool load(const std::string& filename);

    // 获取字符串类型的配置值
    // @param section: 配置项所在的节
    // @param key: 配置项的键
    // @param default_value: 如果找不到对应的键，返回的默认值
    // @return: 配置值或默认值
    std::string getString(const std::string& section, const std::string& key, const std::string& default_value = "") const;

    // 获取整数类型的配置值
    int getInt(const std::string& section, const std::string& key, int default_value = 0) const;

    // 获取浮点数类型的配置值
    double getDouble(const std::string& section, const std::string& key, double default_value = 0.0) const;

    // 获取布尔类型的配置值
    // "true", "yes", "on", "1" (不区分大小写) 会被解析为 true
    // 其他值都被解析为 false
    bool getBool(const std::string& section, const std::string& key, bool default_value = false) const;
    
    // 检查某个节或键是否存在
    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& key) const;

private:
    // 辅助函数，用于去除字符串两端的空白字符
    static std::string trim(const std::string& str);

    // 使用嵌套 map 来存储 INI 数据: map<section, map<key, value>>
    std::map<std::string, std::map<std::string, std::string>> data_;
};