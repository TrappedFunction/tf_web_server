#include "../include/db_cache.h"

namespace TFDB {
LRUCache::LRUCache(size_t capacity) : capacity_(capacity) {}

std::optional<std::string> LRUCache::Get(const std::string& key){
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if(it == map_.end()){
        return std::nullopt; // 未命中
    }

    // 命中，将该节点移动到链表头部（splice是O(1)的）
    list_.splice(list_.begin(), list_, it->second);

    return it->second->second; // 返回value
}

void LRUCache::Put(const std::string& key, const std::string& value){
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if(it != map_.end()){
        // key已存在：更新alue，移动到头部
        it->second->second = value;
        list_.splice(list_.begin(), list_, it->second);
        return;
    }

    // key不存在：插入新节点到头部
    list_.push_front({key, value});
    map_[key] = list_.begin();

    // 检查容量
    if(map_.size() > capacity_){
        // 淘汰末尾
        auto last = list_.end();
        last--;
        map_.erase(last->first); // 从map中删除
        list_.pop_back(); // 从list中删除
    }
}

void LRUCache::Remove(const std::string& key){
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if(it != map_.end()){
        list_.erase(it->second);
        map_.erase(it);
    }
}

void LRUCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    list_.clear();
    map_.clear();
}
}