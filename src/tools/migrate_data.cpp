#include "db_engine.h"
#include "utils/json.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>

using json = nlohmann::json;
using namespace TFDB;

// 配置路径 (根据实际情况调整)
const std::string kJsonDataPath = "../data/";
const std::string kDbPath = "../data/tfdb/";

// 读取 JSON 文件辅助函数
json readJson(const std::string& filename) {
    std::ifstream f(kJsonDataPath + filename);
    if (!f.is_open()) {
        std::cerr << "Warning: Cannot open " << filename << ", skipping." << std::endl;
        return json::array();
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing " << filename << ": " << e.what() << std::endl;
        return json::array();
    }
    return j;
}

void migrateProblems(Engine* db) {
    std::cout << "Migrating Problems..." << std::endl;
    json problems = readJson("problems.json");
    
    std::vector<int> all_ids;
    int max_id = 0;

    int count = 0;
    for (const auto& p : problems) {
        int id = p.value("id", 0);
        if (id == 0) continue;

        // 1. 存入单个题目数据
        // Key: problem:1, Value: {"id":1, "title":...}
        std::string key = "problem:" + std::to_string(id);
        // dump() 将 json 对象序列化为紧凑的字符串
        Status s = db->Put(key, p.dump());
        if (s != kSuccess) {
            std::cerr << "Failed to put " << key << std::endl;
        }

        // 2. 收集 ID
        all_ids.push_back(id);
        max_id = std::max(max_id, id);
        count++;
    }

    // 3. 存入 ID 列表索引
    json ids_json(all_ids);
    db->Put("sys:problem_ids", ids_json.dump());

    // 4. 存入 ID 生成器状态
    db->Put("sys:next_problem_id", std::to_string(max_id));

    std::cout << "  Migrated " << count << " problems." << std::endl;
    std::cout << "  Max ID: " << max_id << std::endl;
}

void migrateFavorites(Engine* db) {
    std::cout << "Migrating Favorites..." << std::endl;
    json favs = readJson("favorites.json");
    
    // 我们也需要一个列表来遍历所有收藏夹，虽然目前需求不强，但为了完备性加上
    // 假设 Key: sys:fav_ids, Value: [1, 2]
    // 假设 Key: sys:next_fav_id
    // 目前收藏夹数据量小，也可以像之前一样存成一个大 JSON 数组 "sys:favorites_all"
    // 但为了和 problem 保持一致，我们拆开存。
    
    // 实际上，之前的 handleGetFavorites 是直接返回整个数组。
    // 为了兼容，我们可以直接把整个数组存为一个 Key。
    // Key: "sys:favorites_all" -> Value: [...]
    // 这样读取时一次取出即可。
    
    // 方案调整：为了让数据库更像数据库，我们还是拆分存，并在内存中维护列表，
    // 或者（更简单的迁移方案）：直接把 favorites.json 的内容原封不动存入一个 Key。
    // 考虑到收藏夹数量通常不多，且需要频繁读取列表，
    // 我们采用：拆分存储数据，聚合存储ID列表。
    
    std::vector<int> all_ids;
    int max_id = 0;
    int count = 0;

    for (const auto& f : favs) {
        int id = f.value("id", 0);
        if (id == 0) continue;

        std::string key = "fav:" + std::to_string(id);
        db->Put(key, f.dump());

        all_ids.push_back(id);
        max_id = std::max(max_id, id);
        count++;
    }
    
    // 存 ID 列表
    json ids_json(all_ids);
    db->Put("sys:fav_ids", ids_json.dump());
    
    // 存 Max ID
    db->Put("sys:next_fav_id", std::to_string(max_id));

    std::cout << "  Migrated " << count << " favorites." << std::endl;
}

int main() {
    // 0. 准备目录
    if (!std::filesystem::exists(kDbPath)) {
        std::filesystem::create_directories(kDbPath);
    }

    // 1. 打开数据库
    std::cout << "Opening DB at " << kDbPath << " ..." << std::endl;
    auto db = Engine::Open(kDbPath);
    if (!db) {
        std::cerr << "Failed to open DB!" << std::endl;
        return 1;
    }

    // 2. 执行迁移
    migrateProblems(db.get());
    migrateFavorites(db.get());

    // 3. 验证一下
    std::string val;
    if (db->Get("sys:next_problem_id", &val) == kSuccess) {
        std::cout << "Verification: Next Problem ID is " << val << std::endl;
    } else {
        std::cerr << "Verification Failed!" << std::endl;
    }

    // 4. 关闭 (RAII 自动处理)
    std::cout << "Migration Done. Closing DB." << std::endl;
    return 0;
}