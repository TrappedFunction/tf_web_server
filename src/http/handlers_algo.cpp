#include "http/handlers.h"
#include "http_utils.h"
#include "http_request.h"
#include "utils/logger.h"
#include "utils/json.hpp" // 引入 json 库
#include "db_engine.h" // 引入数据库引擎
#include <fstream>
#include <vector>
#include <mutex>
#include <algorithm>
#include <set>

using json = nlohmann::json;
extern std::string project_root_path; // 从 main.cpp 引入
extern std::unique_ptr<TFDB::Engine> g_db; // 引入 main.cpp 中定义的全局数据库实例
std::string data_path = "/data/";
std::mutex data_mutex; // 简单的文件读写锁

namespace Handlers {

// 辅助函数：解析查询字符串为 map
std::map<std::string, std::string> parseQueryString(const std::string& query) {
    std::map<std::string, std::string> params;
    std::string key, value;
    size_t start = 0, end;
    while (start < query.length()) {
        end = query.find('=', start);
        if (end == std::string::npos) break;
        key = HttpRequest::urlDecode(query.substr(start, end - start));
        start = end + 1;
        end = query.find('&', start);
        if (end == std::string::npos) end = query.length();
        value = HttpRequest::urlDecode(query.substr(start, end - start));
        params[key] = value;
        start = end + 1;
    }
    return params;
}

// 辅助函数：去除字符串首尾空白
std::string trimString(const std::string& str) {
    const std::string whitespace = " \t\n\r\f\v";
    size_t first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, (last - first + 1));
}

// 分割函数：支持英文逗号 ',' 和中文逗号 "，"
std::vector<std::string> splitAndTrim(const std::string& str) {
    std::vector<std::string> tokens;
    std::string current_token;
    
    for (size_t i = 0; i < str.length(); ) {
        // 检查英文逗号 (1 byte)
        if (str[i] == ',') {
            if (!trimString(current_token).empty()) {
                tokens.push_back(trimString(current_token));
            }
            current_token.clear();
            i++;
        }
        // 检查中文逗号 (UTF-8: 0xEF 0xBC 0x8C, 3 bytes)
        else if (i + 2 < str.length() && 
                 (unsigned char)str[i] == 0xEF && 
                 (unsigned char)str[i+1] == 0xBC && 
                 (unsigned char)str[i+2] == 0x8C) {
            if (!trimString(current_token).empty()) {
                tokens.push_back(trimString(current_token));
            }
            current_token.clear();
            i += 3; // 跳过3个字节
        }
        else {
            current_token += str[i];
            i++;
        }
    }
    
    // 处理最后一个 token
    if (!trimString(current_token).empty()) {
        tokens.push_back(trimString(current_token));
    }
    
    return tokens;
}

// API: 获取所有题目列表 (支持搜索)
// GET /api/problems?search=keyword
void handleGetProblems(const HttpRequest& req, HttpResponse* resp) {
    // 1. 获取所有题目 ID 列表
    std::string ids_json_str;
    json id_list = json::array();
    
    // 读取索引 Key "sys:problem_ids"
    if (g_db->Get("sys:problem_ids", &ids_json_str) == TFDB::kSuccess) {
        try {
            id_list = json::parse(ids_json_str);
        } catch (...) {
            LOG_ERROR << "Failed to parse sys:problem_ids";
        }
    }

    // 2. 解析查询参数
    auto queryParams = parseQueryString(req.getQuery());
    std::string search_term = queryParams["search"];
    std::string tag_filter = queryParams["tag"];
    int filter_fav_id = -1;
    if (queryParams.count("fav_id") && !queryParams["fav_id"].empty()) {
        filter_fav_id = std::stoi(queryParams["fav_id"]);
    }
    int limit = queryParams.count("limit") ? std::stoi(queryParams["limit"]) : 20;
    int offset = queryParams.count("offset") ? std::stoi(queryParams["offset"]) : 0;

    // 3. 准备收藏夹过滤数据
    std::set<int> fav_problem_ids;
    if (filter_fav_id != -1) {
        // 读取指定的收藏夹
        std::string fav_key = "fav:" + std::to_string(filter_fav_id);
        std::string fav_val;
        if (g_db->Get(fav_key, &fav_val) == TFDB::kSuccess) {
            json f = json::parse(fav_val);
            for (auto pid : f["problem_ids"]) fav_problem_ids.insert(pid.get<int>());
        }
    }

    // 4. 准备 "是否收藏" 的 Map (逻辑优化：只读取需要的)
    // 为了性能，我们可能不想在这里遍历所有收藏夹来构建 map。
    // 但为了保持功能一致，我们先用简单方法：读取所有收藏夹 ID，然后查内容。
    // TODO(这部分在读多写少的场景下，性能还是可以接受的，后续可以优化为反向索引)
    std::string fav_ids_str;
    std::map<int, bool> is_fav_map;
    if (g_db->Get("sys:fav_ids", &fav_ids_str) == TFDB::kSuccess) {
        json fav_ids = json::parse(fav_ids_str);
        for (auto fid : fav_ids) {
            std::string fkey = "fav:" + std::to_string(fid.get<int>());
            std::string fval;
            if (g_db->Get(fkey, &fval) == TFDB::kSuccess) {
                json f = json::parse(fval);
                for (auto pid : f["problem_ids"]) is_fav_map[pid.get<int>()] = true;
            }
        }
    }

    // 5. 遍历 ID，查库并过滤
    json filtered = json::array();
    
    // 搜索 ID 逻辑
    bool is_id_search = !search_term.empty() && std::all_of(search_term.begin(), search_term.end(), ::isdigit);
    int search_id = is_id_search ? std::stoi(search_term) : -1;

    // 注意：id_list 中的顺序可能不是有序的（取决于插入顺序），如果需要排序可以在这里 sort
    // std::sort(id_list.begin(), id_list.end()); 

    for (auto& id_val : id_list) {
        int pid = id_val.get<int>();
        
        // 5.1 收藏夹 ID 过滤 (快速过滤，无需查 DB)
        if (filter_fav_id != -1) {
            if (fav_problem_ids.find(pid) == fav_problem_ids.end()) continue;
        }
        // 5.2 搜索 ID 过滤 (快速过滤)
        if (is_id_search && pid != search_id) continue;

        // 5.3 查题目详情
        std::string p_key = "problem:" + std::to_string(pid);
        std::string p_val;
        if (g_db->Get(p_key, &p_val) != TFDB::kSuccess) continue; // 数据不一致？跳过

        json p = json::parse(p_val);
        bool match = true;

        // 5.4 文本搜索过滤
        if (!search_term.empty() && !is_id_search) {
            std::string title = p.value("title", "");
            if (title.find(search_term) == std::string::npos) match = false;
        }

        // 5.5 标签过滤
        if (match && !tag_filter.empty()) {
            bool tag_found = false;
            if (p.contains("tags") && p["tags"].is_array()) {
                for (const auto& t : p["tags"]) {
                    if (t.get<std::string>() == tag_filter) {
                        tag_found = true; break;
                    }
                }
            }
            if (!tag_found) match = false;
        }

        if (match) {
            filtered.push_back({
                {"id", pid},
                {"title", p.value("title", "无标题")},
                {"difficulty", p.value("difficulty", "Easy")},
                {"algorithm", p.value("algorithm", "")},
                {"tags", p.value("tags", json::array())},
                {"is_favorited", is_fav_map[pid]}
            });
        }
    }

    // 6. 分页 (逻辑不变)
    json paged_result = json::array();
    int total_size = filtered.size();
    if (offset < total_size) {
        int end = std::min(offset + limit, total_size);
        for (int i = offset; i < end; ++i) {
            paged_result.push_back(filtered[i]);
        }
    }

    json response_data = {
        {"total", total_size},
        {"data", paged_result}
    };

    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(response_data.dump());
    resp->setContentLength(resp->getBody().length());
}

// 2. 新增 API: 获取所有可用标签
// GET /api/tags
void handleGetAllTags(const HttpRequest& req, HttpResponse* resp) {
    std::set<std::string> unique_tags;
    
    // 1. 获取所有题目 ID
    std::string ids_json_str;
    if (g_db->Get("sys:problem_ids", &ids_json_str) == TFDB::kSuccess) {
        json id_list = json::parse(ids_json_str);
        
        // 2. 遍历题目
        for (auto& id_val : id_list) {
            std::string key = "problem:" + std::to_string(id_val.get<int>());
            std::string val;
            if (g_db->Get(key, &val) == TFDB::kSuccess) {
                json p = json::parse(val);
                // 3. 收集标签
                if (p.contains("tags") && p["tags"].is_array()) {
                    for (const auto& t : p["tags"]) {
                        unique_tags.insert(t.get<std::string>());
                    }
                }
            }
        }
    }

    json tags_array = json::array();
    for (const auto& t : unique_tags) tags_array.push_back(t);
    
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(tags_array.dump());
    resp->setContentLength(resp->getBody().length());
}

// 3. 新增 API: 收藏夹相关
// 获取所有收藏夹
// GET /api/favorites
void handleGetFavorites(const HttpRequest& req, HttpResponse* resp) {
    std::string ids_json_str;
    json result = json::array();

    // 1. 获取收藏夹 ID 列表
    if (g_db->Get("sys:fav_ids", &ids_json_str) == TFDB::kSuccess) {
        json id_list = json::parse(ids_json_str);
        
        // 2. 遍历 ID 获取详情
        for (auto& id_val : id_list) {
            int fid = id_val.get<int>();
            std::string key = "fav:" + std::to_string(fid);
            std::string val;
            if (g_db->Get(key, &val) == TFDB::kSuccess) {
                // 直接把存的 JSON 对象放进结果数组
                result.push_back(json::parse(val));
            }
        }
    }

    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(result.dump());
    resp->setContentLength(resp->getBody().length());
}

// 创建收藏夹
// POST /api/favorites/create (name=xxx)
void handleCreateFavorite(const HttpRequest& req, HttpResponse* resp) {
    std::string name = req.getPostValue("name");
    if (name.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest);
        resp->setBody("{\"error\": \"Name is required\"}");
        resp->setContentType("application/json");
        resp->setContentLength(resp->getBody().length());
        return;
    }
    std::lock_guard<std::mutex> lock(data_mutex);
    // 1. 生成 ID
    std::string max_id_str;
    int new_id = 1;
    if (g_db->Get("sys:next_fav_id", &max_id_str) == TFDB::kSuccess) {
        new_id = std::stoi(max_id_str) + 1;
    }
    g_db->Put("sys:next_fav_id", std::to_string(new_id));

    // 2. 存入数据
    json new_fav = { {"id", new_id}, {"name", name}, {"problem_ids", json::array()} };
    g_db->Put("fav:" + std::to_string(new_id), new_fav.dump());

    // 3. 更新索引
    std::string ids_str;
    json id_list;
    if (g_db->Get("sys:fav_ids", &ids_str) == TFDB::kSuccess) {
        id_list = json::parse(ids_str);
    } else {
        id_list = json::array();
    }
    id_list.push_back(new_id);
    g_db->Put("sys:fav_ids", id_list.dump());
    
    LOG_INFO << "Created favorite list ID: " << new_id << ", Name: " << name;
    
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody("{\"status\": \"success\", \"id\": " + std::to_string(new_id) + "}");
    resp->setContentLength(resp->getBody().length());
}

// 添加题目到收藏夹
// POST /api/favorites/add (fav_id=1&problem_id=5)
void handleAddToFavorite(const HttpRequest& req, HttpResponse* resp) {
    std::string fav_id_str = req.getPostValue("fav_id");
    std::string prob_id_str = req.getPostValue("problem_id");

    if (fav_id_str.empty() || prob_id_str.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest);
        resp->setBody("Missing fav_id or problem_id");
        return;
    }

    int fav_id = std::stoi(fav_id_str);
    int prob_id = std::stoi(prob_id_str);
    std::string key = "fav:" + fav_id_str;

    // 加锁，保护读-改-写过程
    std::lock_guard<std::mutex> lock(data_mutex);

    std::string val;
    if (g_db->Get(key, &val) == TFDB::kSuccess) {
        try {
            json f = json::parse(val);
            
            // 确保 problem_ids 字段存在且是数组
            if (!f.contains("problem_ids") || !f["problem_ids"].is_array()) {
                f["problem_ids"] = json::array();
            }

            // 检查去重
            bool exists = false;
            for (const auto& pid : f["problem_ids"]) {
                if (pid.get<int>() == prob_id) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                f["problem_ids"].push_back(prob_id);
                // 写回数据库
                g_db->Put(key, f.dump());
                LOG_INFO << "Added problem " << prob_id << " to favorite " << fav_id;
            } else {
                LOG_INFO << "Problem " << prob_id << " already in favorite " << fav_id;
            }

            resp->setStatusCode(HttpResponse::k200Ok);
            resp->setBody("{\"status\": \"success\"}");
            resp->setContentType("application/json");

        } catch (const std::exception& e) {
            LOG_ERROR << "JSON parse error in handleAddToFavorite: " << e.what();
            resp->setStatusCode(HttpResponse::k500InternalServerError);
        }
    } else {
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setBody("{\"error\": \"Favorite list not found\"}");
        resp->setContentType("application/json");
    }
    resp->setContentLength(resp->getBody().length());
}

// 实现“点击星星取消收藏”，让用户选择从哪个移除
// POST /api/favorites/remove
// 参数: problem_id (必填), fav_id (选填，不填或-1表示全部移除)
void handleRemoveFromFavorite(const HttpRequest& req, HttpResponse* resp) {
    std::string prob_id_str = req.getPostValue("problem_id");
    if (prob_id_str.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest);
        return;
    }
    int prob_id = std::stoi(prob_id_str);

    std::string fav_id_str = req.getPostValue("fav_id");
    int target_fav_id = fav_id_str.empty() ? -1 : std::stoi(fav_id_str);

    std::lock_guard<std::mutex> lock(data_mutex);

    // 1. 获取所有收藏夹 ID 列表 (因为可能需要遍历所有收藏夹)
    std::string ids_str;
    if (g_db->Get("sys:fav_ids", &ids_str) != TFDB::kSuccess) {
        // 如果连索引都没有，那就什么都不用做
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setBody("{\"status\": \"not_found\"}");
        return;
    }

    json fav_ids_list = json::parse(ids_str);
    bool global_list_changed = false;
    bool any_removed = false;

    // 2. 遍历所有收藏夹
    // 使用迭代器以便安全删除
    for (auto it = fav_ids_list.begin(); it != fav_ids_list.end(); ) {
        int fid = it->get<int>();
        
        // 过滤：如果指定了 target_fav_id，只处理这一个
        if (target_fav_id != -1 && target_fav_id != fid) {
            ++it;
            continue;
        }

        std::string key = "fav:" + std::to_string(fid);
        std::string val;
        
        if (g_db->Get(key, &val) == TFDB::kSuccess) {
            json f = json::parse(val);
            
            if (f.contains("problem_ids") && f["problem_ids"].is_array()) {
                auto& p_ids = f["problem_ids"];
                
                // 从数组中移除题目 ID
                auto original_size = p_ids.size();
                auto pit = std::remove(p_ids.begin(), p_ids.end(), prob_id);
                
                if (pit != p_ids.end()) {
                    p_ids.erase(pit, p_ids.end()); // 真正删除
                    any_removed = true;
                    
                    // **关键：检查是否为空**
                    if (p_ids.empty()) {
                        LOG_INFO << "Favorite list " << fid << " became empty, deleting it.";
                        // 1. 删除收藏夹数据
                        g_db->Delete(key);
                        // 2. 从 ID 列表中移除该收藏夹
                        it = fav_ids_list.erase(it); 
                        global_list_changed = true;
                        continue; // 跳过 ++it，因为 erase 返回了下一个有效迭代器
                    } else {
                        // 收藏夹不为空，更新数据
                        g_db->Put(key, f.dump());
                    }
                }
            }
        }
        ++it;
    }

    // 3. 如果删除了收藏夹，更新全局索引
    if (global_list_changed) {
        g_db->Put("sys:fav_ids", fav_ids_list.dump());
    }

    if (any_removed) {
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setBody("{\"status\": \"removed\"}");
    } else {
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setBody("{\"status\": \"not_found\"}");
    }
    resp->setContentType("application/json");
    resp->setContentLength(resp->getBody().length());
}

// API: 获取单个题目详情
// GET /api/problems/(\d+)
void handleGetProblemDetail(const HttpRequest& req, HttpResponse* resp) {
    const auto& params = req.getRouteParams();
    if (params.empty()) {
        LOG_ERROR << "No route params found for detail request";
        resp->setStatusCode(HttpResponse::k400BadRequest); return;
    }
    std::string id_str = params[0];
    
    // 构造 Key
    std::string key = "problem:" + id_str;
    std::string value;

    // 查库
    TFDB::Status s = g_db->Get(key, &value);

    if (s == TFDB::kSuccess) {
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setContentType("application/json; charset=utf-8");
        // value 本身就是存储进去的 JSON 字符串，直接返回即可
        resp->setBody(value);
        resp->setContentLength(resp->getBody().length());
    } else {
        // kKeyNotFound
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setBody("{}");
        resp->setContentLength(2);
    }
}

// API: 添加新题目
// POST /api/problems
void handleAddProblem(const HttpRequest& req, HttpResponse* resp) {
    LOG_INFO << "Handling Add Problem...";
    
    // 获取 POST 参数
    std::string title = req.getPostValue("title");
    std::string difficulty = req.getPostValue("difficulty");
    std::string desc = req.getPostValue("description");
    std::string algo = req.getPostValue("algorithm");
    std::string idea = req.getPostValue("solution_idea");
    std::string time = req.getPostValue("time_complexity");
    std::string space = req.getPostValue("space_complexity");
    std::string code = req.getPostValue("code");

    if (title.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest);
        resp->setBody("Title cannot be empty");
        return;
    }

    // 构建 tags 数组
    json tags = json::array();
    tags.push_back("New"); // 默认标签
    if (!algo.empty()) {
        std::vector<std::string> algos = splitAndTrim(algo);
        for (const auto& a : algos) {
            tags.push_back(a);
        }
    }

    {
        std::lock_guard<std::mutex> lock(data_mutex); // 加锁

        // 2. 生成新 ID
        // 读取 sys:next_problem_id，自增并写回
        std::string max_id_str;
        int new_id = 1;
        if (g_db->Get("sys:next_problem_id", &max_id_str) == TFDB::kSuccess) {
            new_id = std::stoi(max_id_str) + 1;
        }
        g_db->Put("sys:next_problem_id", std::to_string(new_id));

        // 3. 构造并写入题目数据
        json new_problem = {
            {"id", new_id},
            {"title", title},
            {"difficulty", difficulty},
            {"description", desc},
            {"algorithm", algo},
            {"solution_idea", idea},
            {"time_complexity", time},
            {"space_complexity", space},
            {"code", code},
            {"tags", tags} 
        };
        g_db->Put("problem:" + std::to_string(new_id), new_problem.dump());

        // 4. 更新 ID 索引列表
        std::string ids_str;
        json id_list;
        if (g_db->Get("sys:problem_ids", &ids_str) == TFDB::kSuccess) {
            id_list = json::parse(ids_str);
        } else {
            id_list = json::array();
        }
        id_list.push_back(new_id);
        g_db->Put("sys:problem_ids", id_list.dump());
        
        LOG_INFO << "Added problem ID: " << new_id;
    } // 解锁

    // 返回 302 重定向
    resp->setStatusCode(HttpResponse::k302Found);
    resp->addHeader("Location", "/"); // 重定向到根路径 (首页)
    resp->setContentLength(0);        // 重定向响应通常没有 Body
}

// 前端可以通过 AJAX POST 发送一个 ID 来删除
void handleDeleteProblem(const HttpRequest& req, HttpResponse* resp) {
    std::string id_str = req.getPostValue("id");
    int id = std::stoi(id_str);
    if (id_str.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest);
        return;
    }
    std::lock_guard<std::mutex> lock(data_mutex);
        
    // 1. 删除题目数据
    if (g_db->Delete("problem:" + id_str) == TFDB::kSuccess) {
        // 2. 从 ID 列表中移除
        std::string ids_str;
        if (g_db->Get("sys:problem_ids", &ids_str) == TFDB::kSuccess) {
            json id_list = json::parse(ids_str);
            auto it = std::remove(id_list.begin(), id_list.end(), id);
            if (it != id_list.end()) {
                id_list.erase(it, id_list.end());
                g_db->Put("sys:problem_ids", id_list.dump());
            }
        }
        // 3. 级联删除收藏夹中的引用 (这也是为什么我们需要 sys:fav_ids)
        std::string fav_ids_str;
        if (g_db->Get("sys:fav_ids", &fav_ids_str) == TFDB::kSuccess) {
            json fav_ids = json::parse(fav_ids_str);
            bool fav_list_changed = false;

            // 遍历所有收藏夹
            for (auto it = fav_ids.begin(); it != fav_ids.end(); ) {
                int fid = it->get<int>();
                std::string fkey = "fav:" + std::to_string(fid);
                std::string fval;
                
                if (g_db->Get(fkey, &fval) == TFDB::kSuccess) {
                    json f = json::parse(fval);
                    auto& p_ids = f["problem_ids"];
                    
                    // 移除题目ID
                    auto pit = std::remove(p_ids.begin(), p_ids.end(), id);
                    if (pit != p_ids.end()) {
                        p_ids.erase(pit, p_ids.end());
                        
                        // 检查是否为空，如果为空则删除该收藏夹
                        if (p_ids.empty()) {
                            g_db->Delete(fkey); // 删除收藏夹数据
                            it = fav_ids.erase(it); // 从列表中移除收藏夹ID
                            fav_list_changed = true;
                            continue; // 跳过 ++it
                        } else {
                            // 写回更新后的收藏夹
                            g_db->Put(fkey, f.dump());
                        }
                    }
                }
                ++it;
            }
            
            // 如果删除了收藏夹，更新 sys:fav_ids
            if (fav_list_changed) {
                g_db->Put("sys:fav_ids", fav_ids.dump());
            }
        }

        LOG_INFO << "Deleted problem ID: " << id;
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setBody("{\"status\": \"deleted\"}");
    } else {
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setBody("{\"status\": \"not found\"}");
    }
    resp->setContentType("application/json");
    resp->setContentLength(resp->getBody().length());
}

// API: 获取提问箱列表
// GET /api/questions
void handleGetQuestions(const HttpRequest& req, HttpResponse* resp) {
    
}

// API: 提交问题
// POST /api/questions
void handleAddQuestion(const HttpRequest& req, HttpResponse* resp) {
    
}

// API: 修改题目
// POST /api/problems/update
void handleUpdateProblem(const HttpRequest& req, HttpResponse* resp) {
    std::string id_str = req.getPostValue("id");
    std::string new_algo = req.getPostValue("algorithm");
    if (id_str.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest);
        return;
    }
    int id = std::stoi(id_str);

    // 获取其他字段
    std::string title = req.getPostValue("title");
    std::string difficulty = req.getPostValue("difficulty");
    std::string desc = req.getPostValue("description");
    std::string algo = req.getPostValue("algorithm");
    std::string idea = req.getPostValue("solution_idea");
    std::string time = req.getPostValue("time_complexity");
    std::string space = req.getPostValue("space_complexity");
    std::string code = req.getPostValue("code");

    std::string key = "problem:" + id_str;
    std::string val;
    bool found = false;

    // 加锁不是必须的，因为只操作单条数据，但为了防止读-改-写竞态，加上更好
    std::lock_guard<std::mutex> lock(data_mutex);

    if (g_db->Get(key, &val) == TFDB::kSuccess) {
        json p = json::parse(val);
        // 1. 获取旧的 algorithm
        std::string old_algo = p.value("algorithm", "");

        // 2. 如果 algorithm 发生了变化
        if (old_algo != new_algo) {
            // 解析旧的和新的算法列表
            std::vector<std::string> old_algos = splitAndTrim(old_algo);
            std::vector<std::string> new_algos = splitAndTrim(new_algo);
            
            // 确保 tags 字段存在
            if (!p.contains("tags") || !p["tags"].is_array()) {
                p["tags"] = json::array();
            }
            json& tags = p["tags"];

            // A. 移除旧的：在 old 中但不在 new 中的
            for (const auto& old_a : old_algos) {
                // 如果这个旧标签在新列表中不存在，说明它被删除了
                if (std::find(new_algos.begin(), new_algos.end(), old_a) == new_algos.end()) {
                    // 从 tags 中移除
                    // json array 的移除比较麻烦，通常重建或者 remove_if
                    auto it = std::remove(tags.begin(), tags.end(), old_a);
                    if (it != tags.end()) tags.erase(it);
                }
            }

            // B. 添加新的：在 new 中但不在 old 中的（或者直接检查 tags 里有没有）
            for (const auto& new_a : new_algos) {
                // 检查 tags 里是否已经有了
                bool exists = false;
                for (const auto& t : tags) {
                    if (t.get<std::string>() == new_a) {
                        exists = true; break;
                    }
                }
                if (!exists) {
                    tags.push_back(new_a);
                }
            }
        }

        // 3. 更新其他字段
        p["title"] = title;
        p["difficulty"] = difficulty;
        p["description"] = desc;
        p["algorithm"] = new_algo; // 更新 algo 字段
        p["solution_idea"] = idea;
        p["time_complexity"] = time;
        p["space_complexity"] = space;
        p["code"] = code;
            
        // 3. 写回数据库
        g_db->Put(key, p.dump());
        LOG_INFO << "Updated problem ID: " << id;

        // 重定向回详情页
        resp->setStatusCode(HttpResponse::k302Found);
        resp->addHeader("Location", "/problem.html?id=" + id_str);
        resp->setContentLength(0);
    }else {
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setBody("Problem not found");
        resp->setContentLength(17);
    }
}

} // namespace Handlers

// 注册路由
REGISTER_HANDLER("api_get_problems", handleGetProblems);
REGISTER_HANDLER("api_get_problem_detail", handleGetProblemDetail);
REGISTER_HANDLER("api_add_problem", handleAddProblem);
REGISTER_HANDLER("api_get_questions", handleGetQuestions);
REGISTER_HANDLER("api_add_question", handleAddQuestion);
REGISTER_HANDLER("api_delete_problem", handleDeleteProblem);
REGISTER_HANDLER("api_update_problem", handleUpdateProblem);
REGISTER_HANDLER("api_get_all_tags", handleGetAllTags);
REGISTER_HANDLER("api_get_favorites", handleGetFavorites);
REGISTER_HANDLER("api_create_favorite", handleCreateFavorite);
REGISTER_HANDLER("api_add_to_favorite", handleAddToFavorite);
REGISTER_HANDLER("api_remove_from_favorite", handleRemoveFromFavorite);