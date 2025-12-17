#pragma once
#include <list>
#include <unordered_map>
#include <string>
#include <mutex>
#include <optional>

namespace TFDB {

class LRUCache{
public:
    // capacity: 缓存的最大条目数（不是字节数，简化处理）
    explicit LRUCache(size_t capacity);
    ~LRUCache() = default;

    // 获取数据
    // 如果命中，返回value并将该条目移动到LRU头部
    // 如果未命中，返回std::nullopt
    std::optional<std::string> Get(const std::string& key);

    // 插入数据
    // 如果key已存在，更新value并移动到头部
    // 如果不存在，插入头部；如果容量满了，移除尾部
    void Put(const std::string& key, const std::string& value);

    // 删除数据（当数据库删除key时调用）
    void Remove(const std::string& key);

    // 清空缓存
    void Clear();

private:
    size_t capacity_;

    // 双向链表：存储实际的key-value对
    // head是最近使用的，tail是最久未使用的
    std::list<std::pair<std::string, std::string>> list_;

    // 哈希表：存储key到链表迭代器的映射，实现O(1)查找
    using ListIterator = std::list<std::pair<std::string, std::string>>::iterator;
    std::unordered_map<std::string, ListIterator> map_;

    // 互斥锁：保护list_和map_的并发访问
    std::mutex mutex_;
};
}