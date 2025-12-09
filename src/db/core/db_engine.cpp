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

Engine::Engine() : indexer_(std::make_unique<Indexer>()){}
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

    engine->LoadIndexFromFiles();

    return engine;
}

void Engine::LoadIndexFromFiles(){
    // 1. 遍历归档文件 (已排序)
    for(auto& pair : archived_files_){
        LoadIndexFromFile(pair.first, pair.second.get());
    }
    // 2. 处理活跃文件
    if(active_file_){
        LoadIndexFromFile(active_file_id_, active_file_.get());
    }
}

void Engine::LoadIndexFromFile(uint32_t file_id, DBFile* file) {
    uint64_t offset = 0;
    
    // 获取文件当前的大小
    // DBFile::Open 时已经通过 lseek(SEEK_END) 初始化了 write_off_，所以这里可以直接用
    uint64_t file_size = file->GetWriteOffset();

    // 循环读取直到文件末尾
    while (offset < file_size) {
        // -------------------------------------------------------------
        // 1. 读取并解析 Header (定长)
        // -------------------------------------------------------------
        
        // 边界检查：如果剩余字节连 Header 都装不下，说明是坏尾
        if (offset + kMaxHeaderSize > file_size) {
            std::cerr << "Warning: Incomplete header at file " << file_id 
                      << " offset " << offset << ". Stop loading." << std::endl;
            break; 
        }

        // 读取 Header 字节流
        std::string header_buf = file->Read(offset, kMaxHeaderSize);
        if (header_buf.size() != kMaxHeaderSize) {
            // Read 失败或 EOF (理论上上面已经检查了 size，这里双重保险)
            break;
        }

        // 反序列化 Header
        LogRecordHeader header = Codec::DecodeHeader(header_buf.data());

        // -------------------------------------------------------------
        // 2. 检查 Header 有效性
        // -------------------------------------------------------------
        
        // 如果 key_size 为 0，这通常意味着数据损坏或全是 0 的填充
        if (header.key_size == 0) {
            std::cerr << "Warning: Invalid header (key_size=0) at file " << file_id 
                      << " offset " << offset << ". Stop loading." << std::endl;
            break; 
        }

        // -------------------------------------------------------------
        // 3. 读取 Key 和 Value (变长)
        // -------------------------------------------------------------
        
        uint32_t body_size = header.key_size + header.value_size;
        
        // 边界检查：如果剩余字节不够读 Body，说明是坏尾
        if (offset + kMaxHeaderSize + body_size > file_size) {
            std::cerr << "Warning: Incomplete body at file " << file_id 
                      << " offset " << offset << ". Stop loading." << std::endl;
            break; 
        }

        // 读取 Body 字节流
        std::string body_buf = file->Read(offset + kMaxHeaderSize, body_size);
        if (body_buf.size() != body_size) {
            break;
        }

        // -------------------------------------------------------------
        // 4. 校验 CRC (数据完整性检查)
        // -------------------------------------------------------------
        
        LogRecord record;
        record.type = header.type;
        record.key = body_buf.substr(0, header.key_size);
        record.value = body_buf.substr(header.key_size);
        
        uint32_t actual_crc = Codec::CalculateCRC(record);
        if (actual_crc != header.crc) {
            std::cerr << "Error: CRC mismatch at file " << file_id 
                      << " offset " << offset << ". expected=" << header.crc 
                      << ", actual=" << actual_crc << ". Stop loading." << std::endl;
            // CRC 不匹配意味着数据位翻转或损坏，这是严重错误，必须停止加载该文件
            // 否则可能会恢复出错误的数据
            break; 
        }

        // -------------------------------------------------------------
        // 5. 更新内存索引
        // -------------------------------------------------------------
        
        // 构造索引位置信息
        LogRecordPos pos;
        pos.file_id = file_id;
        pos.offset = offset;

        // 根据操作类型更新索引
        if (header.type == LOG_RECORD_NORMAL) {
            // Put 操作：插入或更新索引
            indexer_->Put(record.key, pos);
        } else if (header.type == LOG_RECORD_DELETED) {
            // Delete 操作：从索引中删除
            indexer_->Delete(record.key);
        } else {
            std::cerr << "Warning: Unknown log type " << (int)header.type 
                      << " at file " << file_id << " offset " << offset << std::endl;
        }

        // -------------------------------------------------------------
        // 6. 移动偏移量，准备读下一条
        // -------------------------------------------------------------
        offset += kMaxHeaderSize + body_size;
    }
    
    // -------------------------------------------------------------
    // 7. 修正活跃文件的写偏移量 (处理坏尾)
    // -------------------------------------------------------------
    
    // 如果当前加载的是活跃文件，且 offset < file_size，
    // 说明文件末尾存在坏数据（incomplete write 或 corruption）。
    // 我们必须把 DBFile 内部的 write_off_ 重置为有效的 offset，
    // 这样下次 Append 时就会覆盖掉这些坏数据，而不是在它们后面继续写（形成空洞）。
    if (file_id == active_file_id_) {
        file->SetWriteOffset(offset);
    }
}

Status Engine::Put(const std::string& key, const std::string& value) {
    if (key.empty()) return kInvalid;

    std::lock_guard<std::mutex> lock(mutex_); // 写锁，保护整个写入流程

    // 1.1 构造 LogRecord
    LogRecord record;
    record.key = key;
    record.value = value;
    record.type = LOG_RECORD_NORMAL;

    // 1.2 追加写入磁盘
    LogRecordPos pos;
    if (!AppendLogRecord(record, &pos)) {
        return kIOError;
    }

    // 1.3 更新内存索引
    indexer_->Put(key, pos);

    return kSuccess;
}

Status Engine::Get(const std::string& key, std::string* value) {
    if (key.empty()) return kInvalid;

    // 2.1 查索引 (Indexer 内部有读写锁，这里不需要加 Engine 的锁)
    LogRecordPos pos;
    if (!indexer_->Get(key, &pos)) {
        return kKeyNotFound;
    }

    // 2.2 读磁盘
    // 注意：ReadLogRecord 可能会抛出异常，这里应该 try-catch
    try {
        LogRecord record = ReadLogRecord(pos);
        
        // 2.3 再次检查类型 (双重保险)
        if (record.type == LOG_RECORD_DELETED) {
            return kKeyNotFound;
        }
        
        *value = record.value;
        return kSuccess;

    } catch (const std::exception& e) {
        std::cerr << "Engine::Get error: " << e.what() << std::endl;
        return kDataCorrupted; // 或者是 kIOError
    }
}

Status Engine::Delete(const std::string& key) {
    if (key.empty()) return kInvalid;

    std::lock_guard<std::mutex> lock(mutex_); // 写锁

    // 3.1 查索引，看 Key 是否存在
    LogRecordPos dummy_pos;
    if (!indexer_->Get(key, &dummy_pos)) {
        return kKeyNotFound; // Key 本就不存在
    }

    // 3.2 构造墓碑消息 (Value 为空，Type 为 DELETED)
    LogRecord record;
    record.key = key;
    record.value = ""; 
    record.type = LOG_RECORD_DELETED;

    // 3.3 写入磁盘 (持久化删除标记)
    LogRecordPos pos;
    if (!AppendLogRecord(record, &pos)) {
        return kIOError;
    }

    // 3.4 从内存索引中删除
    indexer_->Delete(key);

    return kSuccess;
}

bool Engine::AppendLogRecord(const LogRecord& record, LogRecordPos* pos) {
    // 1. 编码
    std::string buf = Codec::Encode(record);
    
    // 2. 记录当前活跃文件的 ID 和 Offset
    pos->file_id = active_file_id_;
    pos->offset = active_file_->GetWriteOffset();
    
    // 3. 写入
    if (!active_file_->Append(buf)) {
        return false;
    }
    
    return true;
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

void Engine::Close() {
    std::lock_guard<std::mutex> lock(mutex_); // 加锁，防止关闭时还有写入
    
    // 1. 刷盘活跃文件
    if (active_file_) {
        active_file_->Sync(); // 调用 fsync
        active_file_->Close();
    }

    // 2. 刷盘并关闭归档文件
    for (auto& pair : archived_files_) {
        if (pair.second) {
            pair.second->Sync();
            pair.second->Close();
        }
    }
}



}