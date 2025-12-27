#pragma once
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <shared_mutex> // C++17
#include <thread>
#include <atomic>
#include <condition_variable>
#include "db_file.h"
#include "db_common.h"
#include "db_index.h"
#include "db_cache.h"

namespace TFDB {

enum Status {
    kSuccess = 0,
    kKeyNotFound = 1,
    kDataCorrupted = 2,
    kIOError = 3,
    kInvalid = 4
};

struct Options {
    std::string dir_path;
    size_t cache_capacity = 1000;
};

class Engine{
public:
    Engine();
    ~Engine();
    Status Merge();

    // 打开数据库 (目前只是打开一个文件)
    // dir_path: 数据库文件所在的目录
    static std::unique_ptr<Engine> Open(const Options& options);

    // **核心接口：写入数据**
    Status Put(const std::string& key, const std::string& value);

    // **核心接口：读取数据**
    Status Get(const std::string& key, std::string* value);

    // **核心接口：删除数据**
    Status Delete(const std::string& key);

    // 关闭数据库
    void Close();

private:
    // 内部辅助：将 LogRecord 写入活跃文件
    // 返回: 写入位置 pos
    bool AppendLogRecord(const LogRecord& record, LogRecordPos* pos);

    // **核心读取接口**：给定偏移量，读取并还原 LogRecord
    // offset: 数据在文件中的起始位置
    LogRecord ReadLogRecord(LogRecordPos pos);

    // **从磁盘文件加载索引**
    void LoadIndexFromFiles();
    
    // **加载单个文件**
    void LoadIndexFromFile(uint32_t file_id, DBFile* file);

    // **后台 Merge 线程函数**
    void MergeWorker();

    // **检查是否需要 Merge**
    // 返回 true 表示应该触发
    bool ShouldMerge();

    std::string dir_path_;
    uint32_t active_file_id_;
    std::unique_ptr<DBFile> active_file_;
    std::map<uint32_t, std::unique_ptr<DBFile>> archived_files_; // 旧文件集合

    // 内存索引
    std::unique_ptr<Indexer> indexer_;
    std::unique_ptr<LRUCache> cache_;
    
    //用于保护 active_file_ 的写入和 indexer_ 的一致性
    mutable std::shared_mutex rw_mutex_; 

    // 互斥锁：保证同一时间只有一个 Merge 线程在运行
    std::mutex merge_mutex_;

    std::thread merge_thread_;
    std::atomic<bool> stop_merge_; // 停止标志
    std::condition_variable merge_cv_;
    std::mutex merge_cv_mutex_; // 专门保护条件变量的锁

    // 统计信息（用于触发策略）
    std::atomic<uint64_t> total_file_size_; // 所有文件总大小
    uint64_t last_merge_size_; // 上次 Merge 后的大小（用于计算增长率）
};
}