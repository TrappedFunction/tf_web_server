#pragma once
#include <string>
#include <memory>
#include "db_common.h"

namespace TFDB {
class DBFile{
public:
    // 传入文件路径
    explicit DBFile(const std::string& filename);
    ~DBFile();

    // 打开或创建一个新的DBFile
    static std::unique_ptr<DBFile> Open(const std::string& filename);

    // 核心写接口：追加写入二进制数据
    bool Append(const std::string& data);

    // 核心读接口：从指定偏移量读取指定长度的数据
    // offset: 文件偏移量
    // length: 要读取的长度
    // 返回: 读取到的二进制字符串，失败返回空字符串或抛出异常
    std::string Read(uint64_t offset, uint32_t length);

    // 获取当前写偏移量（文件大小）
    uint64_t GetWriteOffset() const { return write_off_; }

    // 强制刷盘
    bool Sync();

    void Close();
private:
    std::string filename_;
    int fd_; // 文件描述符
    uint64_t write_off_; // 当前写到的位置，等同于文件大小
};
}