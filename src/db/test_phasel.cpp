#include "include/db_engine.h"
#include <iostream>
#include <cassert>

using namespace TFDB;

int main() {
    // 1. 打开引擎
    auto engine = Engine::Open("."); // 在当前目录
    
    // 2. 写入数据
    LogRecord r1 = {"name", "Alice", LOG_RECORD_NORMAL};
    LogRecord r2 = {"age", "18", LOG_RECORD_NORMAL};
    
    uint64_t pos1, pos2;
    engine->Append(r1, &pos1);
    engine->Append(r2, &pos2);
    
    std::cout << "Wrote r1 at: " << pos1 << std::endl;
    std::cout << "Wrote r2 at: " << pos2 << std::endl;

    // 3. 随机读取
    // 注意：我们必须记住 pos1 和 pos2，这就是索引的作用！
    // 在下一阶段，我们会用 HashMap 来自动记住它们。
    LogRecordPos p1 ,p2;
    p1.file_id = 1;
    p1.offset = pos1;
    p2.file_id = 1;
    p2.offset = pos2;
    
    LogRecord read_r1 = engine->ReadLogRecord(p1);
    std::cout << "Read back r1: " << read_r1.key << " = " << read_r1.value << std::endl;
    assert(read_r1.key == "name");
    assert(read_r1.value == "Alice");

    LogRecord read_r2 = engine->ReadLogRecord(p2);
    std::cout << "Read back r2: " << read_r2.key << " = " << read_r2.value << std::endl;
    assert(read_r2.key == "age");
    assert(read_r2.value == "18");

    std::cout << "=== Phase 1.3 Test Passed! ===" << std::endl;
    return 0;
}