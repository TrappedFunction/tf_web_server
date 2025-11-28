#include "../include/db_engine.h"
#include "../include/db_codec.h"
#include <stdexcept>
#include <iostream>
#include <sys/types.h>
#include <dirent.h>
#include <algorithm>


namespace TFDB{

// 辅助函数：列出目录下的所有 .data 文件并提取 ID
// 返回已排序的 ID 列表
std::vector<uint32_t> GetDataFileIds(const std::string& dir_path) {
    std::vector<uint32_t> file_ids;
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) return file_ids; // 目录可能不存在，稍后 mkdir

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // 筛选以 .data 结尾的文件
        if (name.length() > 5 && name.substr(name.length() - 5) == ".data") {
            // 提取 ID: "000001.data" -> 1
            std::string id_str = name.substr(0, name.length() - 5);
            try {
                file_ids.push_back(std::stoi(id_str));
            } catch (...) {
                // 忽略格式不对的文件
            }
        }
    }
    closedir(dir);
    std::sort(file_ids.begin(), file_ids.end());
    return file_ids;
}

// 辅助函数：生成文件名
std::string FileName(const std::string& dir_path, uint32_t file_id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%09d.data", file_id); // 9位数字，例如 000000001.data
    return dir_path + "/" + std::string(buf);
}

Engine::Engine(){}
Engine::~Engine(){}

std::unique_ptr<Engine> Engine::Open(const std::string& dir_path){
    // 0. 确保目录存在 (mkdir -p)
    // 这里简化，假设目录已由外部创建

    std::unique_ptr<Engine> engine(new Engine());
    engine->dir_path_ = dir_path; // 记得在 Engine 类中添加这个成员

    // 1. 扫描目录，获取所有文件 ID
    std::vector<uint32_t> file_ids = GetDataFileIds(dir_path);

    // 2. 如果没有文件，说明是第一次初始化
    if (file_ids.empty()) {
        engine->active_file_id_ = 0;
        engine->active_file_ = DBFile::Open(FileName(dir_path, 0));
        return engine;
    }

    // 3. 遍历 ID，打开所有文件
    // 最大的 ID 是活跃文件，其余是归档文件（只读）
    for (size_t i = 0; i < file_ids.size(); ++i) {
        uint32_t fid = file_ids[i];
        
        if (i == file_ids.size() - 1) {
            // 最后一个文件是活跃文件
            engine->active_file_id_ = fid;
            engine->active_file_ = DBFile::Open(FileName(dir_path, fid));
        } else {
            // 旧文件放入归档 map
            engine->archived_files_[fid] = DBFile::Open(FileName(dir_path, fid));
        }
    }

    return engine;
}

bool Engine::Append(const LogRecord& record, uint64_t* out_offset){
    // 1. 编码
    std::string buf = Codec::Encode(record);
    
    // 2. 获取写入前的偏移量 (这将是索引中需要的值)
    *out_offset = active_file_->GetWriteOffset();
    
    // 3. 写入磁盘
    return active_file_->Append(buf);
}

LogRecord Engine::ReadLogRecord(LogRecordPos pos) {
     DBFile* file = nullptr;

    // 1. 根据 file_id 找文件
    if (pos.file_id == active_file_id_) {
        file = active_file_.get();
    } else {
        auto it = archived_files_.find(pos.file_id);
        if (it == archived_files_.end()) {
            throw std::runtime_error("File not found for ID: " + std::to_string(pos.file_id));
        }
        file = it->second.get();
    }
    LogRecord record;
    
    // Step 1: 读取固定长度的 Header
    // Header 的长度是 kMaxHeaderSize (13 bytes)
    std::string header_buf = active_file_->Read(pos.offset, kMaxHeaderSize);
    
    // 边界检查：如果读不到 Header，说明 offset 越界或文件损坏
    if (header_buf.size() != kMaxHeaderSize) {
        throw std::runtime_error("Read Header failed: EOF or invalid offset");
    }

    // Step 2: 解码 Header
    LogRecordHeader header = Codec::DecodeHeader(header_buf.data());
    
    // 检查类型是否有效
    if (header.type == 0) {
        throw std::runtime_error("Invalid LogRecordType");
    }
    record.type = header.type;

    // Step 3: 读取 Key 和 Value
    // 此时我们已经知道 Key 和 Value 分别有多长了
    uint32_t body_size = header.key_size + header.value_size;
    
    // 即使 Key/Value 为空（size=0），Read 也能正确处理（返回空串）
    if (body_size > 0) {
        // 偏移量要加上 Header 的长度
        std::string body_buf = active_file_->Read(pos.offset + kMaxHeaderSize, body_size);
        
        if (body_buf.size() != body_size) {
            throw std::runtime_error("Read Body failed: incomplete data");
        }

        // 切分 Key 和 Value
        record.key = body_buf.substr(0, header.key_size);
        record.value = body_buf.substr(header.key_size);
    }

    // Step 4: 校验 CRC
    uint32_t actual_crc = Codec::CalculateCRC(record);
    if (actual_crc != header.crc) {
        throw std::runtime_error("CRC mismatch!");
    }

    return record;
}
}