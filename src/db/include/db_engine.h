#pragma once
#include <string>
#include <memory>
#include <vector>
#include <map>
#include "db_file.h"
#include "db_common.h"

namespace TFDB {

class Engine{
public:
    Engine();
    ~Engine();

    // 打开数据库 (目前只是打开一个文件)
    // dir_path: 数据库文件所在的目录
    static std::unique_ptr<Engine> Open(const std::string& dir_path);

    // 写入接口 (简单封装 DBFile::Append)
    bool Append(const LogRecord& record, uint64_t* out_offset);

    // **核心读取接口**：给定偏移量，读取并还原 LogRecord
    // offset: 数据在文件中的起始位置
    LogRecord ReadLogRecord(LogRecordPos pos);

private:
    std::string dir_path_;
    uint32_t active_file_id_;
    std::unique_ptr<DBFile> active_file_;
    std::map<uint32_t, std::unique_ptr<DBFile>> archived_files_; // 旧文件集合
};
}