#include "../include/db_engine.h"
#include "../include/db_codec.h"
#include "../include/db_iterator.h"
#include <stdexcept>
#include <iostream>
#include <sys/types.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <cstdio> // for remove, rename


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

// 辅助结构：记录搬迁过程中的“线索”
struct MergeHint {
    std::string key;
    LogRecordPos new_pos;
};

Engine::Engine() : indexer_(std::make_unique<Indexer>()),
    cache_(std::make_unique<LRUCache>(1000)){} // TODO 采用了硬编码，后续因设置为配置项
Engine::~Engine(){}

std::unique_ptr<Engine> Engine::Open(const Options& options){
    std::unique_ptr<Engine> engine(new Engine());
    engine->dir_path_ = options.dir_path; // 记得在 Engine 类中添加这个成员

    // 1. 扫描目录，获取所有文件 ID
    std::vector<uint32_t> file_ids = GetDataFileIds(options.dir_path);

    // 2. 如果没有文件，说明是第一次初始化
    if (file_ids.empty()) {
        engine->active_file_id_ = 0;
        engine->active_file_ = DBFile::Open(FileName(options.dir_path, 0));
        return engine;
    }

    // 3. 遍历 ID，打开所有文件
    // 最大的 ID 是活跃文件，其余是归档文件（只读）
    for (size_t i = 0; i < file_ids.size(); ++i) {
        uint32_t fid = file_ids[i];
        
        if (i == file_ids.size() - 1) {
            // 最后一个文件是活跃文件
            engine->active_file_id_ = fid;
            engine->active_file_ = DBFile::Open(FileName(options.dir_path, fid));
        } else {
            // 旧文件放入归档 map
            engine->archived_files_[fid] = DBFile::Open(FileName(options.dir_path, fid));
        }
    }

    engine->LoadIndexFromFiles();

    // 计算初始总大小
    uint64_t total_size = 0;
    if (engine->active_file_) total_size += engine->active_file_->GetWriteOffset();
    for (const auto& pair : engine->archived_files_) {
        total_size += pair.second->GetWriteOffset();
    }
    engine->total_file_size_ = total_size;

    // 启动后台线程
    engine->stop_merge_ = false;
    engine->merge_thread_ = std::thread(&Engine::MergeWorker, engine.get());

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

    // 在持有此锁期间，没有任何其他线程可以 Read 或 Write
    std::unique_lock<std::shared_mutex> lcok(rw_mutex_); // 写锁，保护整个写入流程

    // 构造 LogRecord
    LogRecord record;
    record.key = key;
    record.value = value;
    record.type = LOG_RECORD_NORMAL;

    // 追加写入磁盘
    LogRecordPos pos;
    // AppendLogRecord 内部操作 active_file_，这是非线程安全的，所以必须在锁内
    if (!AppendLogRecord(record, &pos)) {
        return kIOError;
    }

    // 更新内存索引
    indexer_->Put(key, pos);

    // 更新 Cache
    // 策略 A: 更新缓存 (Update) - 适合读多写少
    cache_->Put(key, value);
    
    // 策略 B: 失效缓存 (Invalidate) - 简单，适合写多读少
    // cache_->Remove(key); 

    return kSuccess;
}

Status Engine::Get(const std::string& key, std::string* value) {
    if (key.empty()) return kInvalid;

    // 查Cache
    // Cache内部有锁，不需要加Engine的锁
    auto cache_val = cache_->Get(key);
    if(cache_val.has_value()){
        *value = cache_val.value();
        return kSuccess; // 命中，直接返回，不需要磁盘I/O
    }
    
    // Indexer::Get 内部是线程安全的 (使用 shared_lock)，
    // 但需要保证在查索引和后续读文件期间，文件不会被关闭或删除。
    // 所以在 Engine 层加一个读锁。
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    LogRecordPos pos;
    if (!indexer_->Get(key, &pos)) {
        return kKeyNotFound;
    }

    // 读磁盘
    // 在持有读锁的情况下进行磁盘 I/O 是安全的，允许其他线程同时进行 Get。
    // 只要没有线程持有写锁（即没有 Close 或 File Rotate 发生），
    // active_file_ 和 archived_files_ 的指针就是有效的。
    // Linux 的 pread 是原子且线程安全的。
    // 注意：ReadLogRecord 可能会抛出异常，这里应该 try-catch
    try {
        LogRecord record = ReadLogRecord(pos);
        
        if (record.type == LOG_RECORD_DELETED) {
            // 这里理论上不应该发生，因为 Delete 会清除 Index 和 Cache
            return kKeyNotFound;
        }
        
        *value = record.value;

        // 写入 Cache (回填)
        // 释放读锁后再写 Cache，减少锁持有时间
        lock.unlock(); 
        cache_->Put(key, *value);

        return kSuccess;

    } catch (const std::exception& e) {
        std::cerr << "Engine::Get error: " << e.what() << std::endl;
        return kDataCorrupted; // 或者是 kIOError
    }
}

Status Engine::Delete(const std::string& key) {
    if (key.empty()) return kInvalid;

    std::unique_lock<std::shared_mutex> lock(rw_mutex_); // 写锁

    // 检查 Key 是否存在 (这里虽然 Indexer 内部有锁，但我们在外部加了写锁，所以是安全的)
    LogRecordPos dummy_pos;
    if (!indexer_->Get(key, &dummy_pos)) {
        return kKeyNotFound; // Key 本就不存在
    }

    // 构造墓碑消息 (Value 为空，Type 为 DELETED)
    LogRecord record;
    record.key = key;
    record.value = ""; 
    record.type = LOG_RECORD_DELETED;

    // 写入磁盘 (持久化删除标记)
    LogRecordPos pos;
    if (!AppendLogRecord(record, &pos)) {
        return kIOError;
    }

    // 从内存索引中删除
    indexer_->Delete(key);

    // 从 Cache 中删除
    cache_->Remove(key);

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
    
    // 更新统计
    total_file_size_ += buf.size();

    // **文件滚动 (Rotation)**
    // 既然我们在做 Merge，文件滚动是前置条件。
    // 如果活跃文件太大（例如 100MB），将其转为归档文件，并创建新的活跃文件。
    if (active_file_->GetWriteOffset() >= 100 * 1024 * 1024) { // 100MB
        // 需要写锁，因为我们要修改 files map
        // 注意：AppendLogRecord 已经被 Put 调用，Put 持有写锁，所以这里不需要再加锁
        // 但为了代码结构清晰，通常 Rotate 逻辑会单独提取
        
        // 1. 刷盘
        active_file_->Sync();
        
        // 2. 移入归档
        archived_files_[active_file_id_] = std::move(active_file_);
        
        // 3. 创建新文件
        active_file_id_++;
        std::string new_name = FileName(dir_path_, active_file_id_);
        active_file_ = DBFile::Open(new_name);
        
        // 4. 唤醒 Merge 线程
        // 因为产生了新的归档文件，可能满足了 Merge 条件
        merge_cv_.notify_one();
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

    // 发送停止信号
    stop_merge_ = true;
    merge_cv_.notify_all();

    // 等待线程结束
    if (merge_thread_.joinable()) {
        merge_thread_.join();
    }

    std::unique_lock<std::shared_mutex> lock(rw_mutex_); // 加锁，防止关闭时还有写入
     
    // 刷盘活跃文件
    if (active_file_) {
        active_file_->Sync(); // 调用 fsync
        active_file_->Close();
    }

    // 刷盘并关闭归档文件
    for (auto& pair : archived_files_) {
        if (pair.second) {
            pair.second->Sync();
            pair.second->Close();
        }
    }
}

Status Engine::Merge() {
    // 1. 抢占 Merge 锁，保证单线程 Merge
    // 这把锁不是 rw_mutex_，它只互斥 Merge 操作，不影响正常的 Put/Get
    std::lock_guard<std::mutex> merge_lock(merge_mutex_);

    // 2. 获取需要 Merge 的文件列表
    // 需要一把读锁来安全地拷贝 archived_files_ 列表
    std::map<uint32_t, std::string> merge_candidates;
    uint32_t merge_file_id = 0;
    
    {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        if (archived_files_.empty()) {
            return kSuccess; // 没有旧文件，无需 Merge
        }
        // 拷贝文件路径，避免长时间持有 rw_mutex_
        for (const auto& pair : archived_files_) {
            // 假设我们有获取文件名的方法，这里简单拼接
            // 实际项目中建议把文件名存储在 DBFile 对象中
            char buf[64];
            snprintf(buf, sizeof(buf), "%09d.data", pair.first);
            merge_candidates[pair.first] = dir_path_ + "/" + std::string(buf);
        }
        // 确定新文件的 ID：比当前最大的 ID 还要大
        // 简单策略：active_file_id_ + 1 (注意：这需要配合 Rotate 逻辑，这里简化处理)
        // 为了安全，我们用一个特殊的命名，比如 "merge.data"，Merge 完再重命名
        merge_file_id = active_file_id_ + 1; 
    }

    // 3. 创建临时 Merge 文件
    std::string merge_file_name = dir_path_ + "/merge_pending.data";
    auto merge_file = DBFile::Open(merge_file_name);
    if (!merge_file) return kIOError;

    std::vector<MergeHint> hints;

    // 4. 开始遍历旧文件 (耗时操作，不持锁)
    for (const auto& pair : merge_candidates) {
        uint32_t old_file_id = pair.first;
        std::string old_file_path = pair.second;

        // 打开旧文件用于读取
        auto reader = DBFile::Open(old_file_path);
        if (!reader) continue;

        DBIterator it(reader.get());
        for (; it.Valid(); it.Next()) {
            LogRecord record = it.Record();
            uint64_t record_offset = it.Offset();

            // **核心检查逻辑**
            LogRecordPos index_pos;
            bool is_valid = false;

            // 4.1 查索引 (加读锁)
            {
                std::shared_lock<std::shared_mutex> lock(rw_mutex_);
                if (indexer_->Get(record.key, &index_pos)) {
                    // 如果索引指向的位置 == 当前遍历到的位置，说明这是最新数据
                    if (index_pos.file_id == old_file_id && index_pos.offset == record_offset) {
                        is_valid = true;
                    }
                }
            }

            // 4.2 如果数据有效，搬迁到新文件
            if (is_valid) {
                // 写入临时文件
                std::string buf = Codec::Encode(record);
                uint64_t new_offset = merge_file->GetWriteOffset();
                if (merge_file->Append(buf)) {
                    // 记录线索，稍后统一更新索引
                    hints.push_back({record.key, {merge_file_id, new_offset}});
                } else {
                    return kIOError;
                }
            }
        }
    }
    
    // 刷盘
    merge_file->Sync();

    // 5. 原子提交 (Commit)
    // 这一步必须非常快，因为它需要持有**全局写锁**，阻塞所有读写
    {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);

        // 5.1 再次检查并更新索引
        // 为什么再次检查？因为在步骤 4 (搬迁) 的过程中，用户可能对某个 Key 执行了 Put/Delete
        // 如果发生了更新，索引指向的位置就会变成 Active File，
        // 此时我们刚刚搬迁到 Merge File 的数据就变成了“过时”数据，不能更新索引。
        for (const auto& hint : hints) {
            LogRecordPos current_pos;
            // 只有当索引依然指向“旧文件”时，我们才更新它指向“新合并文件”
            // 怎么判断指向旧文件？我们反向思考：
            // 如果索引指向的是 active_file_，或者索引不存在，或者索引指向了其他文件，都不更新。
            // 但这样判断比较复杂。
            // 简单且正确的逻辑：
            // 我们在步骤 4.1 记录了 "这是有效数据"。
            // 在这里，我们再次 Get。如果 Get 到的 file_id 依然在该批次 merge_candidates 中，
            // 说明在这期间它没有被更新到 Active File。
            // (稍微有点绕，其实只要判断 file_id != active_file_id_ 且存在于 candidates 即可)
            
            if (indexer_->Get(hint.key, &current_pos)) {
                // 如果当前索引指向的文件，是我们刚刚合并过的旧文件之一
                if (merge_candidates.count(current_pos.file_id)) {
                    // 更新索引指向 Merge File
                    indexer_->Put(hint.key, hint.new_pos);
                }
            }
        }

        // 5.2 物理删除旧文件
        // 此时索引已经指向新文件了，旧文件安全可删
        for (const auto& pair : merge_candidates) {
            // 从内存 map 中移除
            archived_files_.erase(pair.first);
            // 从磁盘删除
            ::remove(pair.second.c_str());
        }

        // 5.3 将 Merge 文件转正
        // 这里需要更精细的文件 ID 管理逻辑。
        // 简单起见，我们假设 merge_file_id 是合法的，并将其加入 archived_files_
        // 重命名文件
        char new_name_buf[64];
        snprintf(new_name_buf, sizeof(new_name_buf), "%09d.data", merge_file_id);
        std::string final_path = dir_path_ + "/" + std::string(new_name_buf);
        
        ::rename(merge_file_name.c_str(), final_path.c_str());
        
        // 我们需要重新 Open 一次来获得正确的 fd (或者复用 merge_file 对象并更新 path)
        // 简单起见，所有权转移
        archived_files_[merge_file_id] = std::move(merge_file);
        
        // 注意：merge_file 对象内部的 filename_ 还是旧的，不过 DBFile 读写只认 fd，所以没问题。
        // 但最好还是重新 Open 一次比较严谨。
    }

    return kSuccess;
}

// **后台线程主循环**
void Engine::MergeWorker() {
    while (!stop_merge_) {
        // 1. 等待触发信号 (定期醒来检查)
        // 例如每 30 秒检查一次，或者等待被显式唤醒
        std::unique_lock<std::mutex> lock(merge_cv_mutex_);
        merge_cv_.wait_for(lock, std::chrono::seconds(30));

        if (stop_merge_) break;

        // 2. 检查触发条件
        if (ShouldMerge()) {
            // Log Info: Starting auto merge...
            Merge();
            // Log Info: Auto merge finished.
            
            // 更新统计信息
            // Merge 实际上会减少 total_file_size_，我们需要在 Merge 内部或这里重新计算
            // 简单起见，Merge 内部更新比较准
        }
    }
}

// **触发策略**
// 这是一个经验性的算法，可以根据业务需求调整
bool Engine::ShouldMerge() {
    // 策略 1: 必须有归档文件 (活跃文件不参与 Merge)
    // 这是一个不需要锁的粗略检查
    if (archived_files_.empty()) return false;

    // 策略 2: 文件总大小超过阈值 (例如 1GB)
    // 假设 Options 中有 max_file_size_threshold
    // if (total_file_size_ > 1024 * 1024 * 1024) return true;

    // 策略 3: 无效数据比例 (Space Amplification)
    // 这是一个比较难计算的指标。
    // 精确计算需要遍历文件，这太慢了。
    // 近似计算：
    //   有效数据量 = indexer_->Size() * 平均KV大小 (估算)
    //   总数据量 = total_file_size_
    //   如果 总数据量 / 有效数据量 > 2.0 (即一半是垃圾)，则触发。
    
    // 简单实现：
    // 我们目前只检查归档文件数量。如果旧文件太多（比如超过 5 个），就合并一下。
    {
        std::shared_lock<std::shared_mutex> lock(rw_mutex_);
        if (archived_files_.size() >= 5) {
            return true;
        }
    }

    return false;
}
}