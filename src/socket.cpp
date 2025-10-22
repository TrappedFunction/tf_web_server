#include "socket.h"
#include <unistd.h> // unix的标准头文件，包括文件处理、进程处理
Socket::Socket(int fd) : fd_(fd){

}
Socket::~Socket(){
    if(fd_ > 0){
        ::close(fd_); // ::表示调用的函数close()属于全局函数，而非成员函数
    }
}