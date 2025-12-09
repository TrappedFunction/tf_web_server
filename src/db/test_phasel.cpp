#include "include/db_engine.h"
#include <iostream>
#include <cassert>

using namespace TFDB;

const std::string kDBPath = "./";

void WriteData() {
    auto db = Engine::Open(kDBPath);
    assert(db->Put("key1", "value1") == kSuccess);
    assert(db->Put("key2", "value2") == kSuccess);
    assert(db->Put("key1", "value1_new") == kSuccess); // 更新
    assert(db->Delete("key2") == kSuccess); // 删除
    db->Close();
}

void ReadData() {
    auto db = Engine::Open(kDBPath);
    
    std::string val;
    // key1 应该是最新的值
    assert(db->Get("key1", &val) == kSuccess);
    assert(val == "value1_new");
    
    // key2 应该找不到
    assert(db->Get("key2", &val) == kKeyNotFound);
    
    std::cout << "Recovery Test Passed!" << std::endl;
}

int main() {
    // 第一次运行：写入数据并关闭
    WriteData();
    
    // 第二次运行：重新打开，验证数据是否恢复
    ReadData();
    
    return 0;
}