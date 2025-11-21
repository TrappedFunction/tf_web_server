#include "http/handlers.h"
#include "http_utils.h"
#include "http_request.h"
#include "utils/logger.h"
#include "utils/json.hpp" // 引入 json 库
#include <fstream>
#include <vector>
#include <mutex>
#include <algorithm>
#include <set>

using json = nlohmann::json;
extern std::string project_root_path; // 从 main.cpp 引入
std::string data_path = "/data/";
std::mutex data_mutex; // 简单的文件读写锁

namespace Handlers {

// 辅助函数：读取 JSON 文件
json readJsonFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(data_mutex);
    // 确保路径分隔符正确，Windows下可能需要调整
    std::string full_path = project_root_path + data_path + filename;
    std::ifstream f(full_path);
    if (!f.is_open()) {
        LOG_WARN << "Cannot open file: " << full_path << ", creating new array.";
        return json::array();
    }
    json j;
    try {
        // 如果文件为空，json 解析会抛出异常，所以要处理
        if (f.peek() == std::ifstream::traits_type::eof()) return json::array();
        f >> j;
    } catch (const json::parse_error& e) {
        LOG_ERROR << "JSON parse error in " << filename << ": " << e.what();
        return json::array();
    }
    return j;
}

// 辅助函数：写入 JSON 文件
void writeJsonFile(const std::string& filename, const json& data) {
    std::lock_guard<std::mutex> lock(data_mutex);
    std::string full_path = project_root_path + data_path + filename;
    std::ofstream f(full_path);
    if (f.is_open()) {
        f << data.dump(4);
    } else {
        LOG_ERROR << "Failed to write to file: " << full_path;
    }
}

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

// API: 获取所有题目列表 (支持搜索)
// GET /api/problems?search=keyword
void handleGetProblems(const HttpRequest& req, HttpResponse* resp) {
    json problems = readJsonFile("problems.json");
    json favorites = readJsonFile("favorites.json"); // 读取收藏夹数据
    
    // 1. 解析参数
    auto queryParams = parseQueryString(req.getQuery());
    std::string search_term = queryParams["search"];
    std::string tag_filter = queryParams["tag"]; // 获取 tag 参数
    int limit = queryParams.count("limit") ? std::stoi(queryParams["limit"]) : 20; // 默认每次加载20条
    int offset = queryParams.count("offset") ? std::stoi(queryParams["offset"]) : 0;
    int filter_fav_id = -1;
    if (queryParams.count("fav_id") && !queryParams["fav_id"].empty()) {
        filter_fav_id = std::stoi(queryParams["fav_id"]);
    }
    // 如果指定了收藏夹过滤，先获取该收藏夹包含的题目ID集合
    std::set<int> fav_problem_ids;
    if (filter_fav_id != -1) {
        for (const auto& f : favorites) {
            if (f.value("id", 0) == filter_fav_id) {
                for (auto pid : f["problem_ids"]) fav_problem_ids.insert(pid.get<int>());
                break;
            }
        }
    }
    // 预处理：构建一个 map，记录每个题目ID被哪些收藏夹收藏了
    std::map<int, bool> is_fav_map;
    for (const auto& f : favorites) {
        for (auto pid : f["problem_ids"]) {
            is_fav_map[pid.get<int>()] = true;
        }
    }

    // 2. 过滤 (搜索)
    json filtered_problems = json::array();
    
    // 判断搜索词是否纯数字（用于ID搜索）
    bool is_id_search = !search_term.empty() && 
                        std::all_of(search_term.begin(), search_term.end(), ::isdigit);
    int search_id = is_id_search ? std::stoi(search_term) : -1;

    for (const auto& p : problems) {
        int pid = p.value("id", 0);
        bool match = true;

        // 1. 收藏夹过滤
        if (filter_fav_id != -1) {
            if (fav_problem_ids.find(pid) == fav_problem_ids.end()) match = false;
        }

        if (!search_term.empty()) {
            if (is_id_search) {
                // 按 ID 精确匹配
                if (p.value("id", 0) != search_id) match = false;
            } else {
                // 按标题模糊匹配
                std::string title = p.value("title", "");
                // 简单的大小写不敏感处理（可选）
                if (title.find(search_term) == std::string::npos) match = false;
            }
        }

        // **标签过滤**
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
            filtered_problems.push_back({
                {"id", p.value("id", 0)},
                {"title", p.value("title", "无标题")},
                {"difficulty", p.value("difficulty", "Easy")},
                {"algorithm", p.value("algorithm", "")}, // 列表页可能需要显示算法标签
                {"tags", p.value("tags", json::array())},
                {"is_favorited", is_fav_map[pid]} 
            });
        }
    }

    // 3. 分页 (Slice)
    json paged_result = json::array();
    int total_size = filtered_problems.size();
    
    if (offset < total_size) {
        int end = std::min(offset + limit, total_size);
        for (int i = offset; i < end; ++i) {
            paged_result.push_back(filtered_problems[i]);
        }
    }

    // 返回结果，包含总数以便前端判断是否还有更多
    json response_data = {{"total", filtered_problems.size()}, {"data", paged_result}};

    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(response_data.dump());
    resp->setContentLength(resp->getBody().length());
}

// 2. 新增 API: 获取所有可用标签
// GET /api/tags
void handleGetAllTags(const HttpRequest& req, HttpResponse* resp) {
    json problems = readJsonFile("problems.json");
    std::set<std::string> unique_tags;

    // 遍历所有题目，收集标签
    for (const auto& p : problems) {
        if (p.contains("tags") && p["tags"].is_array()) {
            for (const auto& t : p["tags"]) {
                if (t.is_string()) {
                    unique_tags.insert(t.get<std::string>());
                }
            }
        }
    }

    // 转换为 JSON 数组
    json tags_array = json::array();
    for (const auto& t : unique_tags) {
        tags_array.push_back(t);
    }
    
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(tags_array.dump());
    resp->setContentLength(resp->getBody().length());
}

// 3. 新增 API: 收藏夹相关

// 获取所有收藏夹
// GET /api/favorites
void handleGetFavorites(const HttpRequest& req, HttpResponse* resp) {
    json favs = readJsonFile("favorites.json");
    
    // 如果文件不存在或为空，readJsonFile 会返回空数组，这也是合法的
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(favs.dump());
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
    
    json favs = readJsonFile("favorites.json");
    
    // 生成新 ID
    int new_id = 1;
    if (!favs.empty()) {
        for (const auto& f : favs) {
            if (f.contains("id") && f["id"].is_number()) {
                new_id = std::max(new_id, f["id"].get<int>() + 1);
            }
        }
    }
    
    // 构造新收藏夹对象
    json new_fav = {
        {"id", new_id},
        {"name", name},
        {"problem_ids", json::array()} // 初始为空数组
    };
    
    favs.push_back(new_fav);
    writeJsonFile("favorites.json", favs);
    
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
        return;
    }

    int fav_id = std::stoi(fav_id_str);
    int prob_id = std::stoi(prob_id_str);
    
    json favs = readJsonFile("favorites.json");
    bool found = false;

    for (auto& f : favs) {
        if (f.value("id", 0) == fav_id) {
            // 检查该题目是否已经在收藏夹中
            bool exists = false;
            if (f.contains("problem_ids") && f["problem_ids"].is_array()) {
                for (auto pid : f["problem_ids"]) {
                    if (pid == prob_id) {
                        exists = true;
                        break;
                    }
                }
            } else {
                // 如果字段不存在或不是数组，初始化它
                f["problem_ids"] = json::array();
            }
            
            if (!exists) {
                f["problem_ids"].push_back(prob_id);
                found = true;
            } else {
                // 如果已经存在，我们也视为成功，但不重复添加
                found = true; 
                LOG_INFO << "Problem " << prob_id << " already in favorite " << fav_id;
            }
            break;
        }
    }
    
    if (found) {
        writeJsonFile("favorites.json", favs);
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setContentType("application/json");
        resp->setBody("{\"status\": \"success\"}");
    } else {
        resp->setStatusCode(HttpResponse::k404NotFound); // 收藏夹不存在
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
    
    // 获取 fav_id 参数，默认为 -1 (移除所有)
    std::string fav_id_str = req.getPostValue("fav_id");
    int target_fav_id = fav_id_str.empty() ? -1 : std::stoi(fav_id_str);
    
    json favs = readJsonFile("favorites.json");
    bool changed = false;

    // 使用迭代器遍历，以便安全删除收藏夹
    for (auto fav_it = favs.begin(); fav_it != favs.end(); ) {
        int current_fav_id = fav_it->value("id", 0);
        bool modified_this_fav = false;

        if (target_fav_id == -1 || target_fav_id == current_fav_id) {
            if (fav_it->contains("problem_ids") && (*fav_it)["problem_ids"].is_array()) {
                auto& ids = (*fav_it)["problem_ids"];
                auto it = std::remove_if(ids.begin(), ids.end(), 
                    [prob_id](const json& j){ return j.get<int>() == prob_id; });
                
                if (it != ids.end()) {
                    ids.erase(it, ids.end());
                    changed = true;
                    modified_this_fav = true;
                }
            }
        }

        // **新增：检查是否为空**
        // 只有当这个收藏夹被修改过，且变为空时才删除
        // 或者你可以设定更激进的策略：任何时候遇到空的都删掉
        if (modified_this_fav && (*fav_it)["problem_ids"].empty()) {
            fav_it = favs.erase(fav_it);
            changed = true;
            continue;
        }

        ++fav_it;
    }
    
    if (changed) {
        writeJsonFile("favorites.json", favs);
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setBody("{\"status\":\"removed\"}");
    } else {
        // 即使没找到也返回成功，但在 body 里说明
        resp->setStatusCode(HttpResponse::k200Ok);
        resp->setBody("{\"status\":\"not_found\"}");
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
    // 打印调试日志
    LOG_INFO << "Looking for problem ID: " << params[0];
    int id = 0;
    try {
        id = std::stoi(params[0]);
    } catch (...) {
        LOG_ERROR << "Invalid ID format: " << params[0];
        resp->setStatusCode(HttpResponse::k400BadRequest);
        return;
    }
    
    json problems = readJsonFile("problems.json");
    bool found = false;
    for (const auto& p : problems) {
        // 更加健壮的比较：同时尝试 int 和 string
        int current_id = -1;
        if (p["id"].is_number()) {
            current_id = p["id"].get<int>();
        } else if (p["id"].is_string()) {
            try { current_id = std::stoi(p["id"].get<std::string>()); } catch(...) {}
        }

        if (current_id == id) {
            resp->setStatusCode(HttpResponse::k200Ok);
            resp->setContentType("application/json; charset=utf-8");
            resp->setBody(p.dump());
            resp->setContentLength(resp->getBody().length());
            found = true;
            break;
        }
    }
    
    if (!found) {
        LOG_WARN << "Problem ID " << id << " not found in database.";
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

    json problems = readJsonFile("problems.json");
    // 生成新 ID
    int new_id = 1;
    if (!problems.empty()) {
        // 找到最大的 ID + 1，防止删除后的 ID 冲突
        for (const auto& p : problems) {
            if (p.contains("id") && p["id"].is_number()) {
                new_id = std::max(new_id, p["id"].get<int>() + 1);
            }
        }
    }

    // 构建 tags 数组
    json tags = json::array();
    tags.push_back("New"); // 默认标签
    if (!algo.empty()) {
        tags.push_back(algo); // **自动添加算法类型为标签**
    }
    
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
        {"tags", tags} // TODO暂时写死
    };
    
    problems.push_back(new_problem);
    writeJsonFile("problems.json", problems);

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
    
    json problems = readJsonFile("problems.json");
    
    // 使用 remove_if 删除元素
    auto it = std::remove_if(problems.begin(), problems.end(), 
        [id](const json& j){ return j.value("id", 0) == id; });
        
    if (it != problems.end()) {
        problems.erase(it, problems.end());
        writeJsonFile("problems.json", problems);
        // 从 favorites.json 中级联删除**
        json favs = readJsonFile("favorites.json");
        bool favs_changed = false;
        // 使用迭代器遍历，因为我们要删除元素
        for (auto fav_it = favs.begin(); fav_it != favs.end(); ) {
            if (fav_it->contains("problem_ids") && (*fav_it)["problem_ids"].is_array()) {
                auto& p_ids = (*fav_it)["problem_ids"];
                // 从数组中移除该题目ID
                auto pid_it = std::remove(p_ids.begin(), p_ids.end(), id);
                
                if (pid_it != p_ids.end()) {
                    p_ids.erase(pid_it, p_ids.end());
                    favs_changed = true;
                }
                
                // **新增：检查收藏夹是否为空，若为空则删除收藏夹**
                if (p_ids.empty()) {
                    fav_it = favs.erase(fav_it); // erase 返回下一个有效的迭代器
                    favs_changed = true;
                    continue; // 跳过 ++fav_it
                }
            }
            ++fav_it;
        }
        if (favs_changed) {
            writeJsonFile("favorites.json", favs);
            LOG_INFO << "Cascading delete: updated favorites for problem ID " << id;
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
    json questions = readJsonFile("questions.json");
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(questions.dump());
    resp->setContentLength(resp->getBody().length());
}

// API: 提交问题
// POST /api/questions
void handleAddQuestion(const HttpRequest& req, HttpResponse* resp) {
    std::string content = req.getPostValue("content");
    if (content.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest); return;
    }

    json questions = readJsonFile("questions.json");
    int new_id = questions.empty() ? 1 : questions.back()["id"].get<int>() + 1;
    
    json new_q = {
        {"id", new_id},
        {"content", content},
        {"status", "Unresolved"}
        // {"created_at", Timestamp::now().toString()} 
    };
    
    questions.push_back(new_q);
    writeJsonFile("questions.json", questions);

    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setBody("{\"status\": \"success\"}");
    resp->setContentLength(resp->getBody().length());
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

    json problems = readJsonFile("problems.json");
    bool found = false;

    // 遍历查找并更新
    for (auto& p : problems) {
        if (p.value("id", 0) == id) {
            // 1. 获取旧的 algorithm
            std::string old_algo = p.value("algorithm", "");

            // 2. 更新 tags
            if (p.contains("tags") && p["tags"].is_array()) {
                // 移除旧的 algo 标签 (如果存在且不为空)
                if (!old_algo.empty()) {
                    auto& tags = p["tags"];
                    auto it = std::find(tags.begin(), tags.end(), old_algo);
                    if (it != tags.end()) {
                        tags.erase(it);
                    }
                }
                
                // 添加新的 algo 标签 (如果不为空且不存在)
                if (!new_algo.empty()) {
                    bool exists = false;
                    for (const auto& t : p["tags"]) {
                        if (t.get<std::string>() == new_algo) {
                            exists = true; break;
                        }
                    }
                    if (!exists) {
                        p["tags"].push_back(new_algo);
                    }
                }
            } else {
                // 如果 tags 不存在或无效，初始化它
                p["tags"] = json::array();
                if (!new_algo.empty()) p["tags"].push_back(new_algo);
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
            
            found = true;
            break;
        }
    }

    if (found) {
        writeJsonFile("problems.json", problems);
        LOG_INFO << "Updated problem ID: " << id;
        // 重定向回详情页
        resp->setStatusCode(HttpResponse::k302Found);
        resp->addHeader("Location", "/problem.html?id=" + id_str);
        resp->setContentLength(0);
    } else {
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