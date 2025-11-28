#include "../include/db_file.h"
#include <fcntl.h>    // for open, O_RDWR, etc.
#include <unistd.h>   // for read, write, close, lseek
#include <stdexcept>
#include <iostream>

namespace TFDB {

DBFile::DBFile(const std::string& filename) :
    filename_(filename), fd_(-1), write_off_(0){}

DBFile::~DBFile(){
    Close();
}

std::unique_ptr<DBFile> DBFile::Open(const std::string& filename){
    std::unique_ptr<DBFile> file(new DBFile(filename));

    // 1. 打开文件
    // O_CREAT: 不存在则创建
    // O_RDWR: 读写模式
    // O_APPEND: 追加模式 (虽然我们手动维护 offset，但这个标志是个好习惯)
    // 0644: 权限 (rw-r--r--)
    file->fd_ = ::open(filename.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);

    if(file->fd_ == -1){
        throw std::runtime_error("Failed to open file: " + filename);
    }

    // 2. 获取文件大小，初始化写偏移量
    // lseek 返回移动后的偏移量，移到 END 就是文件大小
    file->write_off_ = ::lseek(file->fd_, 0, SEEK_END);
    if(file->write_off_ == -1){
        throw std::runtime_error("Failed to lseek file: " + filename);
    }

    return file;
}

bool DBFile::Append(const std::string& data){
    // write 系统调用
    // data.data() 返回 const char*，data.size() 是字节数
    ssize_t write_len = ::write(fd_, data.data(), data.size());

    if(write_len == -1){
        std::cerr << "Write error for file: " << filename_ << std::endl;
        return false;
    }

    if(static_cast<ssize_t>(write_len) != data.size()){
        std::cerr << "Incomplete write for file: " << filename_ << std::endl;
        return false;
    }

    // 更新写偏移量
    write_off_ += write_len;
    return true;
}

std::string DBFile::Read(uint64_t offset, uint32_t length){
    if(length == 0) return "";

    // 准备缓冲区
    std::string buf;
    buf.resize(length);

    // pread 系统调用：原子性的定位+读取
    // 相比于 lseek + read，pread 是线程安全的！
    // 它不会改变文件指针的当前位置
    ssize_t read_len = ::pread(fd_, &buf[0], length, offset);

    if(read_len == -1){
        std::cerr << "Read error for file: " << filename_ << std::endl;
        return "";
    }

    // 如果读到的数据少于请求的数据 (例如读到了文件末尾)
    if (read_len < length) {
        // 实际只读到了 read_len 个字节，调整 string 大小
        buf.resize(read_len);
    }

    return buf;
}

bool DBFile::Sync(){
    // fsync: 强制将内核缓冲区的数据刷入物理磁盘
    if (::fsync(fd_) == -1) {
        return false;
    }
    return true;
}

void DBFile::Close() {
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}
}