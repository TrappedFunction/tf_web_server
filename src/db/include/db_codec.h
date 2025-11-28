#pragma once
#include "db_common.h"
#include <vector>
#include <string>

namespace TFDB {
class Codec{
public:
    // 编码，将LogRecode打包成二进制字节流
    // 返回打包后的字节流字符串
    static std::string Encode(const LogRecord& record);

    // 解码头部：从缓冲区中解析出Header信息
    // buf:必须至少含有kMaxHeaderSize 个字节
    static LogRecordHeader DecodeHeader(const char* buf);

    // 计算CRC校验码
    static uint32_t CalculateCRC(const LogRecord& record);
};

}