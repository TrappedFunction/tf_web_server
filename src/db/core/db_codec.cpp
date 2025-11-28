#include "../include/db_codec.h"
#include <cstring>
#include <vector>

namespace TFDB{

// CRC32 表生成逻辑
static uint32_t crc32_table[256];
static bool crc32_table_computed = false;

static void make_crc32_table(void) {
    uint32_t c;
    for (int n = 0; n < 256; n++) {
        c = (uint32_t)n;
        for (int k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xedb88320L ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc32_table[n] = c;
    }
    crc32_table_computed = true;
}

static uint32_t update_crc(uint32_t crc, const unsigned char *buf, size_t len) {
    uint32_t c = crc;
    if (!crc32_table_computed)
        make_crc32_table();
    for (size_t n = 0; n < len; n++) {
        c = crc32_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}

// 供外部调用的标准 CRC32 函数
static uint32_t crc32(const unsigned char *buf, size_t len) {
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

// -------------------------------------------------------------

uint32_t Codec::CalculateCRC(const LogRecord& record) {
    // CRC 应该包含整个记录的内容：Type + KeySize + ValSize + Key + Value
    // 为了简单且高效，我们这里只校验 Key 和 Value (生产环境建议校验全部)
    // 或者，我们可以先构造出一个临时 buffer，对 buffer 计算 CRC
    
    // 方法：分段计算。CRC32 支持流式计算。
    // 初始值
    uint32_t crc = 0xffffffffL;
    
    // 1. Type (1 byte)
    crc = update_crc(crc, (const unsigned char*)&record.type, 1);
    
    // 2. Key
    crc = update_crc(crc, (const unsigned char*)record.key.data(), record.key.size());
    
    // 3. Value
    crc = update_crc(crc, (const unsigned char*)record.value.data(), record.value.size());
    
    return crc ^ 0xffffffffL;
}

std::string Codec::Encode(const LogRecord& record){
    // 准备Header
    LogRecordHeader header;
    header.type = record.type;
    header.key_size = static_cast<uint32_t>(record.key.size());
    header.value_size = static_cast<uint32_t>(record.value.size());
    header.crc = CalculateCRC(record);

    // 计算总长度
    size_t total_size = kMaxHeaderSize + header.key_size + header.value_size;

    // 分配空间
    std::string buf;
    buf.resize(total_size);
    char* ptr = &buf[0];

    // 序列化 Header (按顺序拷贝)
    // 假设是小端序机器，且不跨架构传输。
    // 生产环境应转换为大端序 (Network Byte Order)

    // CRC (4 bytes)
    memcpy(ptr, &header.crc, 4);
    ptr += 4;
    
    // Type (1 byte)
    memcpy(ptr, &header.type, 1);
    ptr += 1;
    
    // Key Size (4 bytes)
    memcpy(ptr, &header.key_size, 4);
    ptr += 4;
    
    // Value Size (4 bytes)
    memcpy(ptr, &header.value_size, 4);
    ptr += 4;

    // 序列化 Key 和 Value
    memcpy(ptr, record.key.data(), header.key_size);
    ptr += header.key_size;
    
    memcpy(ptr, record.value.data(), header.value_size);
    
    return buf;
}

LogRecordHeader Codec::DecodeHeader(const char* buf){
    LogRecordHeader header;
    const char* ptr = buf;

    // 被拷贝数据是否被拷贝完即无用了，那是不是可以直接使用move()
    // 反向操作：从 buffer 中拷贝出各个字段
    memcpy(&header.crc, ptr, 4);
    ptr += 4;
    
    memcpy(&header.type, ptr, 1);
    ptr += 1;
    
    memcpy(&header.key_size, ptr, 4);
    ptr += 4;
    
    memcpy(&header.value_size, ptr, 4);
    
    return header;
}
}