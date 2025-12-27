#pragma once
#include "db_file.h"
#include "db_codec.h"
#include <memory>

namespace TFDB {
class DBIterator {
public:
    // 构造函数：传入要扫描的DBFile对象
    // DBFile的生命周期必须比Iterator长
    explicit DBIterator(DBFile* file);
    ~DBIterator() = default;

    // 检查当前位置是否有效
    bool Valid() const { return valid_; }

    // 移动到下一条记录
    void Next();

    // 获取当前记录
    const LogRecord& Record() const { return current_record_; }
    
    // 获取当前记录在文件中的偏移量 (Merge 时需要用来比对索引)
    uint64_t Offset() const { return current_offset_; }

private:
    DBFile* file_;
    uint64_t offset_; // 下一条要读的位置
    uint64_t current_offset_; // 当前记录的起始位置
    LogRecord current_record_;
    bool valid_;
    uint64_t file_size_;
};
}