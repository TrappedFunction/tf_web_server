#pragma once
#include <string>
#include <cstdint>

// 定义命名空间，避免污染全局
namespace TFDB {
// -----------业务数据类型（用户传进的数据）----------------

// 操作类型：目前为Append-Only，删除是写入的一种(写入一个墓碑标记)
enum LogRecordType : char {
    LOG_RECORD_NORMAL = 1, // 正常数据
    LOG_RECODE_DELETED = 2 // 删除标记
};

// 内存中的日志记录对象
struct LogRecord{
    std::string key;
    std::string value;
    LogRecordType type;
};

// -----------------存储层数据布局---------------

// 磁盘上每条数据的头部信息
// 格式：[crc(4)][type(1)][key_size(4)][value_size(4)]
struct LogRecordHeader {
    uint32_t crc;        // 校验码，保证数据完整性
    LogRecordType type;  // 类型
    uint32_t key_size;   // Key 长度
    uint32_t value_size; // Value 长度
};

// 头部固定长度常量
const int kMaxHeaderSize = sizeof(uint32_t) * 3 + sizeof(LogRecordType); // 13

// ------------------------索引位置信息---------------------

// 数据存在哪个文件？哪个偏移量？多大？
struct LogRecordPos{
    uint32_t file_id; // 文件ID (例如 1 代表 000001.data)
    uint64_t offset;  // 文件内的偏移量
};

}