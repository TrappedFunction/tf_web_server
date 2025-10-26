#include "buffer.h"
#include <sys/uio.h> // 允许单次系统调用读取多个不连续的内存缓冲区
#include <unistd.h>
#include <errno.h>

ssize_t Buffer::readFd(int fd, int* saved_errno){
    char extrabuf[65536]; // 64KB的栈上备用缓冲区
    struct iovec vec[2]; // 两个IO向量，系统可以向两个地方读取数据，对应两个缓冲区
    const size_t writable = writableBytes();

    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1; // 决定使用的缓冲区个数，主缓冲区剩余空间小于64KB，使用两个
    const ssize_t n = ::readv(fd, vec, iovcnt); // 一次性读取数据到指定的缓冲区
    
    if(n < 0){
        *saved_errno = errno; // 保存错误码
    }else if(static_cast<size_t>(n) <= writable){ // 全部写入主缓冲区
        write_index_ += n;
    }else{ // 分散到两个缓冲区
        write_index_ = buffer_.size();
        append(extrabuf, n - writable); // 追加额外数据
    }
    return n;
}

void Buffer::makeSpace(size_t len){
    if(writableBytes() + prependableBytes() < len + kCheapPrepend){
        // 空间不足，需要扩容
        buffer_.resize(write_index_ + len);
    }else{
        // 内部腾挪，将可读数据移动到前面
        size_t readable = readableBytes();
        std::copy(begin() + readableBytes(),
                  begin() + writableBytes(),
                  begin() + kCheapPrepend);
        reader_index_ = kCheapPrepend;
        write_index_ = reader_index_ + readable;
    }
}