#include "server.h"
#include <iostream>

int main(int argc, char* argv[]){
    if(argc != 2){
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    try{
        uint16_t port = std::stoi(argv[1]);
        Server my_server(port);
        my_server.start();
    }catch(const std::exception& e){
        // 异常处理代码
        std::cerr << "Exception caught in main: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}