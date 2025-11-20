#include "http/handlers.h"
#include "http_utils.h"
#include "http_request.h"
#include "utils/logger.h"
#include "utils/json.hpp" // 引入 json 库
#include <fstream>
#include <vector>
#include <mutex>
#include <algorithm>

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
    
    // 1. 解析参数
    auto queryParams = parseQueryString(req.getQuery());
    std::string search_term = queryParams["search"];
    int limit = queryParams.count("limit") ? std::stoi(queryParams["limit"]) : 20; // 默认每次加载20条
    int offset = queryParams.count("offset") ? std::stoi(queryParams["offset"]) : 0;

    // 2. 过滤 (搜索)
    json filtered_problems = json::array();
    
    // 判断搜索词是否纯数字（用于ID搜索）
    bool is_id_search = !search_term.empty() && 
                        std::all_of(search_term.begin(), search_term.end(), ::isdigit);
    int search_id = is_id_search ? std::stoi(search_term) : -1;

    for (const auto& p : problems) {
        bool match = true;
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

        if (match) {
            filtered_problems.push_back({
                {"id", p.value("id", 0)},
                {"title", p.value("title", "无标题")},
                {"difficulty", p.value("difficulty", "Easy")},
                {"algorithm", p.value("algorithm", "")}, // 列表页可能需要显示算法标签
                {"tags", p.value("tags", json::array())}
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
    json response_data = {
        {"total", total_size},
        {"data", paged_result}
    };

    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json; charset=utf-8");
    resp->setBody(response_data.dump());
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
        {"tags", {"New"}} // TODO暂时写死
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
    if (id_str.empty()) {
        resp->setStatusCode(HttpResponse::k400BadRequest);
        return;
    }
    int id = std::stoi(id_str);
    
    json problems = readJsonFile("problems.json");
    
    // 使用 remove_if 删除元素
    auto it = std::remove_if(problems.begin(), problems.end(), 
        [id](const json& j){ return j.value("id", 0) == id; });
        
    if (it != problems.end()) {
        problems.erase(it, problems.end());
        writeJsonFile("problems.json", problems);
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
            p["title"] = title;
            p["difficulty"] = difficulty;
            p["description"] = desc;
            p["algorithm"] = algo;
            p["solution_idea"] = idea;
            p["time_complexity"] = time;
            p["space_complexity"] = space;
            p["code"] = code;
            // tags 可以根据需要处理，这里简化不更新 tags
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