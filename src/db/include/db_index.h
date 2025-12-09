#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <shared_mutex> // C++17 读写锁
#include "db_common.h"

namespace TFDB {

class Indexer {
public:
    Indexer() = default;
    ~Indexer() = default;

    // 添加或更新索引
    // key: 键
    // pos: 数据在磁盘上的位置
    void Put(const std::string& key, LogRecordPos pos);

    // 查询索引
    // key: 键
    // 返回: 如果存在返回对应的 pos，否则返回一个标识无效的 pos (比如 file_id=0 或 offset=-1)
    // 为了更清晰，我们可以返回 bool 和出参
    bool Get(const std::string& key, LogRecordPos* pos);

    // 删除索引
    // key: 键
    // 返回: 如果删除成功（之前存在）返回 true
    bool Delete(const std::string& key);

    // 获取当前索引中的 Key 数量
    size_t Size();

private:
    // 核心数据结构：哈希表
    std::unordered_map<std::string, LogRecordPos> index_;
    
    // 读写锁：允许多个线程同时 Get，但 Put/Delete 时必须互斥
    // mutable 关键字允许在 const 成员函数（如 Size）中修改锁的状态
    mutable std::shared_mutex mutex_; 
};

}