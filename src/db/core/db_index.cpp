#include "../include/db_index.h"

namespace TFDB {

void Indexer::Put(const std::string& key, LogRecordPos pos) {
    // 获取写锁 (独占)
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 插入或更新
    // unordered_map 的 operator[] 会在 key 不存在时创建新条目，存在时更新
    index_[key] = pos;
}

bool Indexer::Get(const std::string& key, LogRecordPos* pos) {
    // 获取读锁 (共享)
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = index_.find(key);
    if (it == index_.end()) {
        return false;
    }
    
    *pos = it->second;
    return true;
}

bool Indexer::Delete(const std::string& key) {
    // 获取写锁 (独占)
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // erase 返回删除的元素数量 (0 或 1)
    return index_.erase(key) > 0;
}

size_t Indexer::Size() {
    // 获取读锁 (共享)
    // 即使只是读取 size，在多线程环境下不加锁也是不安全的，
    // 因为可能此时另一个线程正在插入，导致内部结构变化。
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return index_.size();
}

}