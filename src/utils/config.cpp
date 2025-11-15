#include "utils/config.h"
#include <fstream>
#include <sstream>
#include <algorithm> // for std::transform

bool Config::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    data_.clear();
    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        line = trim(line);

        // 忽略空行和注释
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // 解析 [section]
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            current_section = trim(current_section);
        }
        // 解析 key = value
        else {
            size_t delimiter_pos = line.find('=');
            if (delimiter_pos != std::string::npos) {
                std::string key = line.substr(0, delimiter_pos);
                std::string value = line.substr(delimiter_pos + 1);
                key = trim(key);
                value = trim(value);
                
                if (!current_section.empty() && !key.empty()) {
                    data_[current_section][key] = value;
                }
            }
        }
    }
    return true;
}

std::string Config::getString(const std::string& section, const std::string& key, const std::string& default_value) const {
    auto section_it = data_.find(section);
    if (section_it != data_.end()) {
        auto key_it = section_it->second.find(key);
        if (key_it != section_it->second.end()) {
            return key_it->second;
        }
    }
    return default_value;
}

int Config::getInt(const std::string& section, const std::string& key, int default_value) const {
    std::string value_str = getString(section, key);
    if (value_str.empty()) {
        return default_value;
    }
    try {
        return std::stoi(value_str);
    } catch (const std::invalid_argument& ia) {
        // Log warning: invalid integer format
        return default_value;
    } catch (const std::out_of_range& oor) {
        // Log warning: integer out of range
        return default_value;
    }
}

double Config::getDouble(const std::string& section, const std::string& key, double default_value) const {
    std::string value_str = getString(section, key);
    if (value_str.empty()) {
        return default_value;
    }
    try {
        return std::stod(value_str);
    } catch (const std::invalid_argument& ia) {
        return default_value;
    } catch (const std::out_of_range& oor) {
        return default_value;
    }
}

bool Config::getBool(const std::string& section, const std::string& key, bool default_value) const {
    std::string value_str = getString(section, key);
    if (value_str.empty()) {
        return default_value;
    }
    // 转换为小写进行比较
    std::transform(value_str.begin(), value_str.end(), value_str.begin(), ::tolower);
    if (value_str == "true" || value_str == "yes" || value_str == "on" || value_str == "1") {
        return true;
    }
    if (value_str == "false" || value_str == "no" || value_str == "off" || value_str == "0") {
        return false;
    }
    return default_value;
}

bool Config::hasSection(const std::string& section) const {
    return data_.find(section) != data_.end();
}

bool Config::hasKey(const std::string& section, const std::string& key) const {
    auto section_it = data_.find(section);
    if (section_it != data_.end()) {
        return section_it->second.find(key) != section_it->second.end();
    }
    return false;
}

std::string Config::trim(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return ""; // 字符串全是空白
    }
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}