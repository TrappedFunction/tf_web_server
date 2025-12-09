#pragma once
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include "db_file.h"
#include "db_common.h"
#include "db_index.h"

namespace TFDB {

enum Status {
    kSuccess = 0,
    kKeyNotFound = 1,
    kDataCorrupted = 2,
    kIOError = 3,
    kInvalid = 4
};

class Engine{
public:
    Engine();
    ~Engine();

    // 打开数据库 (目前只是打开一个文件)
    // dir_path: 数据库文件所在的目录
    static std::unique_ptr<Engine> Open(const std::string& dir_path);

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

    std::string dir_path_;
    uint32_t active_file_id_;
    std::unique_ptr<DBFile> active_file_;
    std::map<uint32_t, std::unique_ptr<DBFile>> archived_files_; // 旧文件集合

    // 内存索引
    std::unique_ptr<Indexer> indexer_;
    
    // **新增：全局写锁**
    // 尽管 Indexer 有锁，DBFile 也有原子写，但在高层逻辑上（写文件 -> 更新索引）
    // 这两个步骤必须是原子的，否则可能出现数据写了但索引没更新的情况。
    // 读操作不需要这把锁，因为 Indexer 本身支持并发读。
    std::mutex mutex_; 
};
}