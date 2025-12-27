#include "../include/db_iterator.h"
#include <iostream>

namespace TFDB {

DBIterator::DBIterator(DBFile* file) 
    : file_(file), offset_(0), current_offset_(0), valid_(false) {
    if (file_) {
        file_size_ = file->GetWriteOffset();
        // 构造完成后，自动读取第一条记录
        Next(); 
    }
}

void DBIterator::Next() {
    // 如果已经读完或文件无效，直接标记无效
    if (!file_ || offset_ >= file_size_) {
        valid_ = false;
        return;
    }

    // 记录这条数据的起始位置
    current_offset_ = offset_;

    // -------------------------------------------------------------
    // 1. 读取 Header
    // -------------------------------------------------------------
    if (offset_ + kMaxHeaderSize > file_size_) {
        valid_ = false; return;
    }

    std::string header_buf = file_->Read(offset_, kMaxHeaderSize);
    if (header_buf.size() != kMaxHeaderSize) {
        valid_ = false; return;
    }

    LogRecordHeader header = Codec::DecodeHeader(header_buf.data());

    // -------------------------------------------------------------
    // 2. 检查 Header 有效性
    // -------------------------------------------------------------
    if (header.key_size == 0 && header.value_size == 0) {
        valid_ = false; return;
    }

    // -------------------------------------------------------------
    // 3. 读取 Body
    // -------------------------------------------------------------
    uint32_t body_size = header.key_size + header.value_size;
    if (offset_ + kMaxHeaderSize + body_size > file_size_) {
        valid_ = false; return;
    }

    std::string body_buf = file_->Read(offset_ + kMaxHeaderSize, body_size);
    if (body_buf.size() != body_size) {
        valid_ = false; return;
    }

    // -------------------------------------------------------------
    // 4. 组装 Record 并校验
    // -------------------------------------------------------------
    LogRecord record;
    record.type = header.type;
    record.key = body_buf.substr(0, header.key_size);
    record.value = body_buf.substr(header.key_size);

    uint32_t actual_crc = Codec::CalculateCRC(record);
    if (actual_crc != header.crc) {
        std::cerr << "DBIterator: CRC mismatch at offset " << current_offset_ << std::endl;
        valid_ = false;
        return;
    }

    // -------------------------------------------------------------
    // 5. 成功，更新状态
    // -------------------------------------------------------------
    current_record_ = std::move(record);
    valid_ = true;
    
    // 更新 offset_ 指向下一条记录的开始
    offset_ += kMaxHeaderSize + body_size;
}

}