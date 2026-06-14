// controllers/RelationshipController.cc
#include "RelationshipController.h"
#include "AuthController.h"
#include "ResponseUtils.h"
#include "models/Member.h"
#include "models/DbUtils.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>
#include <vector>
#include <queue>
#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

using namespace api::v1;
using namespace drogon;
using Json::Value;
using myutils::createApiResponse;


std::string RelationshipController::getTokenFromHeader(const HttpRequestPtr& req) {
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0) {
        return authHeader.substr(7);
    }
    return "";
}

int RelationshipController::getUserIdFromToken(const std::string& token) {
    if (token.empty()) {
        return 0;
    }

    auto [userId, username] = AuthController::validateToken(token);
    return userId;
}

bool RelationshipController::checkMemberAccess(int member_id, int user_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT g.user_id FROM Member m "
            "JOIN Genealogy g ON m.genealogy_id = g.genealogy_id "
            "WHERE m.member_id = ?",
            member_id
        );

        if (!result.empty()) {
            int owner_id = result[0]["user_id"].as<int>();
            if (owner_id == user_id) {
                return true;  // 创建者
            }

            // 检查共享权限
            auto shareResult = client->execSqlSync(
                "SELECT permission FROM GenealogyShare gs "
                "JOIN Member m ON gs.genealogy_id = m.genealogy_id "
                "WHERE m.member_id = ? AND gs.user_id = ?",
                member_id, user_id
            );

            if (!shareResult.empty()) {
                return true;  // 有查看权限
            }
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "检查成员权限错误: " << e.what() << std::endl;
        return false;
    }
}

// 获取成员的配偶ID
int RelationshipController::getSpouseId(int member_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT spouse_id FROM Member WHERE member_id = ?",
            member_id
        );

        if (!result.empty() && !result[0]["spouse_id"].isNull()) {
            int sid = result[0]["spouse_id"].as<int>();
            if (sid > 0) return sid;
        }
    } catch (const std::exception& e) {
        std::cerr << "查询配偶ID错误: " << e.what() << std::endl;
    }
    return 0;
}

// ============================================================
// 使用递归CTE获取包含配偶的亲属路径
// 优化：一次SQL查询获取所有祖先+配偶，替换原来的N+1查询
// ============================================================
std::vector<int> RelationshipController::getAncestorPathWithSpouse(int member_id, int max_depth) {
    auto client = app().getDbClient();
    std::vector<int> result;
    std::unordered_set<int> seen;

    try {
        // ★ 优化：使用 ancestor_path 替代递归 CTE
        // ancestor_path 格式: /1/862/2717/.../member_id
        // 解析即可得到完整祖先链，无需递归查询
        auto pathResult = client->execSqlSync(
            "SELECT ancestor_path, spouse_id FROM Member WHERE member_id = ?", member_id);

        if (!pathResult.empty()) {
            // 解析 ancestor_path
            std::string ancestor_path = dbutils::safeStr(pathResult[0], "ancestor_path");
            int spouse_id = dbutils::safeInt(pathResult[0], "spouse_id");

            if (!ancestor_path.empty() && ancestor_path[0] == '/') {
                std::vector<int> ids;
                size_t start = 1;
                while (start < ancestor_path.length()) {
                    size_t end = ancestor_path.find('/', start);
                    if (end == std::string::npos) end = ancestor_path.length();
                    std::string id_str = ancestor_path.substr(start, end - start);
                    if (!id_str.empty()) {
                        int id = std::stoi(id_str);
                        if (id != member_id) ids.push_back(id);
                    }
                    start = end + 1;
                }
                // 反转：根→成员 变成 成员→根（近→远）
                std::reverse(ids.begin(), ids.end());

                // 应用深度限制
                for (int id : ids) {
                    if (static_cast<int>(result.size()) >= max_depth) break;
                    if (seen.insert(id).second) {
                        result.push_back(id);
                    }
                }
            }

            // 如果有配偶，也获取配偶的祖先路径
            if (spouse_id > 0 && seen.insert(spouse_id).second) {
                result.push_back(spouse_id);

                auto spResult = client->execSqlSync(
                    "SELECT ancestor_path FROM Member WHERE member_id = ?", spouse_id);

                if (!spResult.empty()) {
                    std::string sp_path = dbutils::safeStr(spResult[0], "ancestor_path");
                    if (!sp_path.empty() && sp_path[0] == '/') {
                        std::vector<int> sp_ids;
                        size_t start = 1;
                        while (start < sp_path.length()) {
                            size_t end = sp_path.find('/', start);
                            if (end == std::string::npos) end = sp_path.length();
                            std::string id_str = sp_path.substr(start, end - start);
                            if (!id_str.empty()) {
                                int id = std::stoi(id_str);
                                if (id != spouse_id) sp_ids.push_back(id);
                            }
                            start = end + 1;
                        }
                        std::reverse(sp_ids.begin(), sp_ids.end());

                        for (int id : sp_ids) {
                            if (static_cast<int>(result.size()) >= max_depth * 2) break;
                            if (seen.insert(id).second) {
                                result.push_back(id);
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "获取包含配偶的亲属路径错误: " << e.what() << std::endl;
    }

    return result;
}

Json::Value RelationshipController::getMemberDetails(int member_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT * FROM Member WHERE member_id = ?",
            member_id
        );

        if (!result.empty()) {
            models::Member member;
            member.member_id = result[0]["member_id"].as<int>();
            member.genealogy_id = result[0]["genealogy_id"].as<int>();
            member.name = result[0]["name"].as<std::string>();
            member.gender = result[0]["gender"].as<std::string>();
            member.birth_date = dbutils::safeStr(result[0], "birth_date");
            member.death_date = dbutils::safeStr(result[0], "death_date");
            member.biography = dbutils::safeStr(result[0], "biography");
            member.father_id = dbutils::safeInt(result[0], "father_id");
            member.mother_id = dbutils::safeInt(result[0], "mother_id");
            member.spouse_id = dbutils::safeInt(result[0], "spouse_id");
            member.generation = dbutils::safeInt(result[0], "generation");

            return member.toJson();
        }
    } catch (const std::exception& e) {
        std::cerr << "获取成员详情错误: " << e.what() << std::endl;
    }
    return Json::Value();
}

// ============================================================
// 使用递归CTE获取祖先路径（一次SQL查询替代N+1次查询）
// 实验要求 SQL 2: 递归CTE查询所有祖先
// ★ 修复：添加去重（UNION ALL 在脏数据下可能产生重复）
// ============================================================
std::vector<int> RelationshipController::getAncestorPath(int member_id, int max_depth) {
    auto client = app().getDbClient();
    std::vector<int> ancestors;

    try {
        std::string sql = R"(
WITH RECURSIVE ancestors AS (
    SELECT member_id, father_id, mother_id, 0 as depth
    FROM Member WHERE member_id = ?

    UNION ALL

    SELECT m.member_id, m.father_id, m.mother_id, a.depth + 1
    FROM Member m
    JOIN ancestors a ON (m.member_id = a.father_id OR m.member_id = a.mother_id)
    WHERE a.depth < ?
        AND (a.father_id IS NOT NULL OR a.mother_id IS NOT NULL)
        AND (a.father_id > 0 OR a.mother_id > 0)
)
SELECT member_id FROM ancestors WHERE depth > 0 ORDER BY depth
)";

        auto result = client->execSqlSync(sql, member_id, max_depth);

        // ★ 修复：使用 set 去重，防止脏数据下 UNION ALL 产生重复节点
        std::unordered_set<int> seen;
        for (const auto& row : result) {
            int mid = row["member_id"].as<int>();
            if (seen.insert(mid).second) {
                ancestors.push_back(mid);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "查询祖先路径错误: " << e.what() << std::endl;
    }

    return ancestors;
}

// ============================================================
// 获取后代ID列表（迭代法 + 根节点快速通道）
//
// 【快速通道】如果成员是根节点（无父无母），利用 genealogy_id+generation 复合索引：
//   SELECT ... WHERE genealogy_id=X AND generation>G AND generation<=G+depth
//   一条 SQL 即可获取全部后代，毫秒级响应。
//
// 【通用通道】非根节点使用迭代法：
//   层1: WHERE father_id=X OR mother_id=X → children
//   层2: WHERE father_id IN (children) OR mother_id IN (children) → grandchildren
//   ...直到无子节点或达到深度限制
// ============================================================
std::vector<int> RelationshipController::getDescendantPath(int member_id, int max_depth) {
    auto client = app().getDbClient();
    std::vector<int> descendants;

    try {
        // 先获取成员的 genealogy + generation + 是否根节点
        auto info = client->execSqlSync(
            "SELECT genealogy_id, generation, father_id, mother_id FROM Member WHERE member_id = ?",
            member_id);
        if (info.empty()) return descendants;

        int gid = info[0]["genealogy_id"].as<int>();
        int gen = info[0]["generation"].as<int>();
        int fid = dbutils::safeInt(info[0], "father_id");
        int mid = dbutils::safeInt(info[0], "mother_id");
        bool is_root = (fid <= 0 && mid <= 0);

        if (is_root) {
            // ★ 快速通道：根节点的后代 = 同族谱中所有 generation > gen 的成员
            //   利用 idx_member_genealogy_gen(genealogy_id, generation) 复合索引精准范围扫描
            auto result = client->execSqlSync(
                "SELECT member_id FROM Member WHERE genealogy_id = ? AND generation > ?"
                " AND generation <= ? ORDER BY generation",
                gid, gen, gen + max_depth);
            for (const auto& row : result) {
                descendants.push_back(row["member_id"].as<int>());
            }
        } else {
            // 通用通道：迭代法逐层查询
            std::unordered_set<int> seen;
            seen.insert(member_id);
            std::vector<int> current_ids;
            current_ids.push_back(member_id);

            for (int level = 0; level < max_depth && !current_ids.empty(); level++) {
                std::vector<int> next_ids;
                size_t batch_start = 0;
                while (batch_start < current_ids.size()) {
                    size_t batch_end = std::min(batch_start + 2000, current_ids.size());
                    std::string in_list;
                    for (size_t i = batch_start; i < batch_end; i++) {
                        if (i > batch_start) in_list += ",";
                        in_list += std::to_string(current_ids[i]);
                    }
                    batch_start = batch_end;

                    auto result = client->execSqlSync(
                        "SELECT member_id FROM Member WHERE father_id IN (" + in_list +
                        ") UNION SELECT member_id FROM Member WHERE mother_id IN (" + in_list + ")");

                    for (const auto& row : result) {
                        int child_id = row["member_id"].as<int>();
                        if (seen.insert(child_id).second) {
                            descendants.push_back(child_id);
                            next_ids.push_back(child_id);
                        }
                    }
                }
                current_ids = std::move(next_ids);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "查询后代路径错误: " << e.what() << std::endl;
    }

    return descendants;
}

// 查询祖先接口实现
void RelationshipController::getAncestors(const HttpRequestPtr& req,
                                        std::function<void(const HttpResponsePtr&)>&& callback,
                                        int member_id) {
    try {
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }

        int user_id = getUserIdFromToken(token);
        if (user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }

        if (!checkMemberAccess(member_id, user_id)) {
            callback(createApiResponse(403, "无权查看此成员"));
            return;
        }

        // max_depth: 追溯深度，0或不传表示追溯到根节点（无深度限制）
        int max_depth = 0;
        auto max_depth_str = req->getParameter("max_depth");
        if (!max_depth_str.empty()) {
            try {
                max_depth = std::stoi(max_depth_str);
                if (max_depth < 0) max_depth = 0;
            } catch (...) {
                callback(createApiResponse(400, "最大深度参数无效"));
                return;
            }
        }

        // max_results: 前端可配置，默认0=不限
        int max_results = 0;
        auto max_results_str = req->getParameter("max_results");
        if (!max_results_str.empty()) {
            try {
                max_results = std::stoi(max_results_str);
                if (max_results < 0) max_results = 0;
            } catch (...) {
                callback(createApiResponse(400, "最大结果数参数无效"));
                return;
            }
        }

        auto client = app().getDbClient();
        try {
            // ★ 核心优化：使用 ancestor_path（Materialized Path）替代递归 CTE
            // ancestor_path 已存储从根节点到该成员的完整线性路径（优先父系），
            // 格式如 /1/862/2717/.../33332。直接解析此字段即可获得祖先链，
            // 无需递归 CTE，2 条 SQL 即可完成，无论深度多大都能在毫秒级响应。

            // 步骤1：获取目标成员的 ancestor_path 和 generation
            auto pathResult = client->execSqlSync(
                "SELECT member_id, name, gender, birth_date, death_date, genealogy_id,"
                " father_id, mother_id, spouse_id, generation, ancestor_path"
                " FROM Member WHERE member_id = ?", member_id);

            if (pathResult.empty()) {
                callback(createApiResponse(404, "成员不存在"));
                return;
            }

            std::string ancestor_path = dbutils::safeStr(pathResult[0], "ancestor_path");
            int target_generation = dbutils::safeInt(pathResult[0], "generation");

            // 构建目标成员的 JSON
            Value target = getMemberDetails(member_id);

            // 步骤2：解析 ancestor_path 获取祖先 ID 列表
            // 格式：/1/862/2717/4799/.../33332
            // 排除最后一个（即目标成员自身）
            std::vector<int> ancestor_ids;
            if (!ancestor_path.empty() && ancestor_path[0] == '/') {
                size_t start = 1;  // 跳过开头的 '/'
                while (start < ancestor_path.length()) {
                    size_t end = ancestor_path.find('/', start);
                    if (end == std::string::npos) {
                        end = ancestor_path.length();
                    }
                    std::string id_str = ancestor_path.substr(start, end - start);
                    if (!id_str.empty()) {
                        int id = std::stoi(id_str);
                        if (id != member_id) {
                            ancestor_ids.push_back(id);
                        }
                    }
                    start = end + 1;
                }
            }

            // ancestor_path 是根→成员的顺序，需要反转得到 成员→根（近→远）
            std::reverse(ancestor_ids.begin(), ancestor_ids.end());

            // 步骤3：应用 max_depth 限制
            int total_ancestors = static_cast<int>(ancestor_ids.size());
            if (max_depth > 0 && total_ancestors > max_depth) {
                ancestor_ids.resize(max_depth);
                total_ancestors = max_depth;
            }

            Value data;
            Value ancestorsArray(Json::arrayValue);

            if (!ancestor_ids.empty()) {
                // 步骤4：批量查询所有祖先 + 目标成员 + 配偶的详细信息
                std::string allIds;
                allIds.reserve(ancestor_ids.size() * 6);
                for (size_t i = 0; i < ancestor_ids.size(); i++) {
                    if (i > 0) allIds += ",";
                    allIds += std::to_string(ancestor_ids[i]);
                }
                // 也把目标成员加进去（后面构建 target 时直接用）
                allIds += "," + std::to_string(member_id);

                std::string batchSql = "SELECT member_id, name, gender, birth_date, death_date,"
                    " genealogy_id, father_id, mother_id, spouse_id, generation"
                    " FROM Member WHERE member_id IN (" + allIds + ")";

                auto batchResult = client->execSqlSync(batchSql);

                // 构建 member_id → 行的映射
                std::unordered_map<int, size_t> rowMap;
                for (size_t i = 0; i < batchResult.size(); i++) {
                    int mid = batchResult[i]["member_id"].as<int>();
                    rowMap[mid] = i;
                }

                // 收集需要补充查询的配偶 ID
                std::set<int> spouseIds;

                // 步骤5：按路径顺序构建祖先列表（深度 1 = 最近的祖先，即父母）
                for (size_t idx = 0; idx < ancestor_ids.size(); idx++) {
                    int aid = ancestor_ids[idx];
                    auto it = rowMap.find(aid);
                    if (it == rowMap.end()) continue;  // 数据不一致，跳过

                    const auto& row = batchResult[it->second];
                    int depth = static_cast<int>(idx) + 1;  // depth 1 = 父母

                    models::Member member;
                    member.member_id = aid;
                    member.genealogy_id = row["genealogy_id"].as<int>();
                    member.name = row["name"].as<std::string>();
                    member.gender = row["gender"].as<std::string>();
                    member.birth_date = dbutils::safeStr(row, "birth_date");
                    member.death_date = dbutils::safeStr(row, "death_date");
                    member.father_id = dbutils::safeInt(row, "father_id");
                    member.mother_id = dbutils::safeInt(row, "mother_id");
                    member.spouse_id = dbutils::safeInt(row, "spouse_id");
                    member.generation = dbutils::safeInt(row, "generation");

                    Value ancestorJson = member.toJson();
                    ancestorJson["depth"] = depth;

                    // 计算关系标签
                    std::string relation;
                    if (depth == 1) relation = "父亲/母亲";
                    else if (depth == 2) relation = "祖父/祖母/外祖父/外祖母";
                    else if (depth == 3) relation = "曾祖父/曾祖母";
                    else if (depth == 4) relation = "高祖父/高祖母";
                    else relation = "第" + std::to_string(depth) + "代祖先";
                    ancestorJson["relation"] = relation;

                    // 补充配偶信息（如果配偶不在已查询的列表中）
                    if (member.spouse_id > 0) {
                        if (rowMap.find(member.spouse_id) == rowMap.end()) {
                            spouseIds.insert(member.spouse_id);
                        } else {
                            // 配偶已在批量查询中，直接引用
                            const auto& spRow = batchResult[rowMap[member.spouse_id]];
                            Value spouseJson;
                            spouseJson["member_id"] = spRow["member_id"].as<int>();
                            spouseJson["name"] = spRow["name"].as<std::string>();
                            spouseJson["gender"] = spRow["gender"].as<std::string>();
                            spouseJson["generation"] = dbutils::safeInt(spRow, "generation");
                            ancestorJson["spouse"] = spouseJson;
                        }
                    }

                    ancestorsArray.append(std::move(ancestorJson));
                }

                // 步骤6：补充查询缺失的配偶信息
                if (!spouseIds.empty()) {
                    std::string spIds;
                    for (int sid : spouseIds) {
                        if (!spIds.empty()) spIds += ",";
                        spIds += std::to_string(sid);
                    }
                    auto spResult = client->execSqlSync(
                        "SELECT member_id, name, gender, generation FROM Member WHERE member_id IN (" + spIds + ")");

                    std::unordered_map<int, size_t> spMap;
                    for (size_t i = 0; i < spResult.size(); i++) {
                        spMap[spResult[i]["member_id"].as<int>()] = i;
                    }

                    // 重新遍历 ancestorsArray 补充配偶
                    for (auto& ancestorJson : ancestorsArray) {
                        int sp_id = ancestorJson.isMember("spouse_id") ?
                            ancestorJson["spouse_id"].asInt() : 0;
                        if (sp_id > 0 && !ancestorJson.isMember("spouse")) {
                            auto sit = spMap.find(sp_id);
                            if (sit != spMap.end()) {
                                const auto& spRow = spResult[sit->second];
                                Value spouseJson;
                                spouseJson["member_id"] = spRow["member_id"].as<int>();
                                spouseJson["name"] = spRow["name"].as<std::string>();
                                spouseJson["gender"] = spRow["gender"].as<std::string>();
                                spouseJson["generation"] = dbutils::safeInt(spRow, "generation");
                                ancestorJson["spouse"] = spouseJson;
                            }
                        }
                    }
                }

                // 步骤7：应用 max_results 截断
                if (max_results > 0 && ancestorsArray.size() > static_cast<size_t>(max_results)) {
                    ancestorsArray.resize(max_results);
                }
            }

            data["target_member"] = target;
            data["ancestors"] = ancestorsArray;
            data["total"] = static_cast<int>(ancestorsArray.size());
            data["path_length"] = total_ancestors;  // ancestor_path 上的完整祖先数
            data["max_depth"] = max_depth;
            data["max_results"] = max_results;

            callback(createApiResponse(200, "获取成功", data));

        } catch (const std::exception& e) {
            std::cerr << "查询祖先错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "查询祖先异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// 查询后代接口实现
void RelationshipController::getDescendants(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          int member_id) {
    try {
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }

        int user_id = getUserIdFromToken(token);
        if (user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }

        if (!checkMemberAccess(member_id, user_id)) {
            callback(createApiResponse(403, "无权查看此成员"));
            return;
        }

        // max_depth: 向下遍历深度，0或不传表示遍历所有后代
        int max_depth = 0;
        auto max_depth_str = req->getParameter("max_depth");
        if (!max_depth_str.empty()) {
            try {
                max_depth = std::stoi(max_depth_str);
                if (max_depth < 0) max_depth = 0;
            } catch (...) {
                callback(createApiResponse(400, "最大深度参数无效"));
                return;
            }
        }

        // max_results: 前端可配置，默认0=不限（返回所有唯一后代）
        int max_results = 0;
        auto max_results_str = req->getParameter("max_results");
        if (!max_results_str.empty()) {
            try {
                max_results = std::stoi(max_results_str);
                if (max_results < 0) max_results = 0;
            } catch (...) {
                callback(createApiResponse(400, "最大结果数参数无效"));
                return;
            }
        }

        auto client = app().getDbClient();
        try {
            // ★ 后代查询优化：根节点快速通道 + 通用迭代通道
            // 【快速通道】如果成员是根节点（无父无母），利用 genealogy_id+generation 复合索引：
            //   SELECT ... WHERE genealogy_id=X AND generation>G AND generation<=G+depth
            //   一条 SQL 毫秒级获取全部后代 (idx_member_genealogy_gen)
            // 【通用通道】非根节点使用迭代法逐层查询

            // 先检查是否为根节点
            int member_gen = 0, member_gid = 0;
            bool is_root = false;
            {
                auto info = client->execSqlSync(
                    "SELECT genealogy_id, generation, father_id, mother_id FROM Member WHERE member_id = ?",
                    member_id);
                if (!info.empty()) {
                    member_gid = info[0]["genealogy_id"].as<int>();
                    member_gen = info[0]["generation"].as<int>();
                    int fid = dbutils::safeInt(info[0], "father_id");
                    int mid_p = dbutils::safeInt(info[0], "mother_id");
                    is_root = (fid <= 0 && mid_p <= 0);
                }
            }

            int effective_depth = (max_depth > 0) ? max_depth : 100;
            std::unordered_map<int, int> descendant_depth;  // member_id → depth

            if (is_root) {
                // ★ 快速通道：一条 SQL 利用 genealogy_id+generation 复合索引
                //   根节点的后代 = 同族谱中 generation > root_gen 的全体成员
                auto result = client->execSqlSync(
                    "SELECT member_id, generation FROM Member"
                    " WHERE genealogy_id = ? AND generation > ? AND generation <= ?"
                    " ORDER BY generation",
                    member_gid, member_gen, member_gen + effective_depth);
                for (const auto& row : result) {
                    int mid_r = row["member_id"].as<int>();
                    int gen_r = row["generation"].as<int>();
                    descendant_depth[mid_r] = gen_r - member_gen;  // depth = generation offset
                }
            } else {
                // 通用通道：迭代法逐层查询
                std::vector<int> current_ids;
                current_ids.push_back(member_id);

                for (int level = 0; level < effective_depth && !current_ids.empty(); level++) {
                    std::vector<int> next_ids;
                    size_t batch = 0;
                    while (batch < current_ids.size()) {
                        size_t batch_end = std::min(batch + 2000, current_ids.size());
                        std::string in_list;
                        for (size_t i = batch; i < batch_end; i++) {
                            if (i > batch) in_list += ",";
                            in_list += std::to_string(current_ids[i]);
                        }
                        batch = batch_end;

                        auto result = client->execSqlSync(
                            "SELECT member_id FROM Member WHERE father_id IN (" + in_list +
                            ") UNION SELECT member_id FROM Member WHERE mother_id IN (" + in_list + ")");

                        for (const auto& row : result) {
                            int child_id = row["member_id"].as<int>();
                            if (descendant_depth.find(child_id) == descendant_depth.end()) {
                                descendant_depth[child_id] = level + 1;
                                next_ids.push_back(child_id);
                            }
                        }
                    }
                    current_ids = std::move(next_ids);
                }
            }

            // 获取目标成员信息
            Value target = getMemberDetails(member_id);

            Value data;
            Value descendantsArray(Json::arrayValue);

            if (!descendant_depth.empty()) {
                // 步骤2：查询所有后代的详细信息
                //   根节点快速通道：一条 SQL 范围扫描，利用 genealogy+generation 索引
                //   通用通道：分批 IN 查询
                std::vector<std::pair<int, std::vector<drogon::orm::Result>>> batch_results;

                if (is_root) {
                    // ★ 根节点：一条 SQL 直接获取全量详情（避免 10 次 IN 批查询）
                    //   idx_member_genealogy_gen(genealogy_id, generation) 索引精准范围扫描
                    auto detailResult = client->execSqlSync(
                        "SELECT member_id, name, gender, birth_date, death_date, genealogy_id,"
                        " father_id, mother_id, spouse_id, generation"
                        " FROM Member WHERE genealogy_id = ? AND generation > ? AND generation <= ?"
                        " ORDER BY generation",
                        member_gid, member_gen, member_gen + effective_depth);
                    // 包装为与通用通道兼容的格式
                    batch_results.push_back({0, {std::move(detailResult)}});
                } else {
                    // 通用通道：分批 IN 查询
                    std::vector<std::string> id_batches;
                    std::string current_batch;
                    int batch_count = 0;
                    for (const auto& [mid, _] : descendant_depth) {
                        if (batch_count > 0) current_batch += ",";
                        current_batch += std::to_string(mid);
                        batch_count++;
                        if (batch_count >= 5000) {
                            id_batches.push_back(std::move(current_batch));
                            current_batch.clear();
                            batch_count = 0;
                        }
                    }
                    if (!current_batch.empty()) id_batches.push_back(std::move(current_batch));

                    for (const auto& ids : id_batches) {
                        auto res = client->execSqlSync(
                            "SELECT member_id, name, gender, birth_date, death_date, genealogy_id,"
                            " father_id, mother_id, spouse_id, generation"
                            " FROM Member WHERE member_id IN (" + ids + ")");
                        batch_results.push_back({0, {std::move(res)}});
                    }
                }

                // 构建 member_id → (batch_index, result_index) 检索映射
                std::unordered_map<int, std::pair<int, size_t>> member_index;
                for (size_t bi = 0; bi < batch_results.size(); bi++) {
                    for (size_t ri = 0; ri < batch_results[bi].second[0].size(); ri++) {
                        int mid = batch_results[bi].second[0][ri]["member_id"].as<int>();
                        member_index[mid] = {static_cast<int>(bi), ri};
                    }
                }

                // 步骤3：构建排序后的后代列表
                struct DescEntry {
                    int depth;
                    int generation;
                    std::string birth_date;
                    Value json;
                };
                std::vector<DescEntry> sorted;
                sorted.reserve(descendant_depth.size());

                for (const auto& [mid, depth] : descendant_depth) {
                    auto it = member_index.find(mid);
                    if (it == member_index.end()) continue;

                    const auto& row = batch_results[it->second.first].second[0][it->second.second];

                    models::Member member;
                    member.member_id = mid;
                    member.genealogy_id = row["genealogy_id"].as<int>();
                    member.name = row["name"].as<std::string>();
                    member.gender = row["gender"].as<std::string>();
                    member.birth_date = dbutils::safeStr(row, "birth_date");
                    member.death_date = dbutils::safeStr(row, "death_date");
                    member.father_id = dbutils::safeInt(row, "father_id");
                    member.mother_id = dbutils::safeInt(row, "mother_id");
                    member.spouse_id = dbutils::safeInt(row, "spouse_id");
                    member.generation = dbutils::safeInt(row, "generation");

                    Value descJson = member.toJson();
                    descJson["depth"] = depth;
                    sorted.push_back({depth, member.generation, member.birth_date, std::move(descJson)});
                }

                // 排序: depth ASC, generation ASC, birth_date ASC
                std::sort(sorted.begin(), sorted.end(),
                    [](const DescEntry& a, const DescEntry& b) {
                        if (a.depth != b.depth) return a.depth < b.depth;
                        if (a.generation != b.generation) return a.generation < b.generation;
                        return a.birth_date < b.birth_date;
                    });

                // 应用 max_results 限制
                size_t limit = (max_results > 0) ? static_cast<size_t>(max_results) : sorted.size();
                for (size_t i = 0; i < std::min(sorted.size(), limit); i++) {
                    descendantsArray.append(std::move(sorted[i].json));
                }
            }

            data["target_member"] = target;
            data["descendants"] = descendantsArray;
            data["total"] = static_cast<int>(descendantsArray.size());
            data["unique_descendants"] = static_cast<int>(descendant_depth.size());
            data["max_depth"] = max_depth;
            data["max_results"] = max_results;

            callback(createApiResponse(200, "获取成功", data));

        } catch (const std::exception& e) {
            std::cerr << "查询后代错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "查询后代异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// 查询亲缘关系接口实现
void RelationshipController::getRelationship(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }

        int user_id = getUserIdFromToken(token);
        if (user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }

        auto member_id1_str = req->getParameter("member_id1");
        auto member_id2_str = req->getParameter("member_id2");

        if (member_id1_str.empty() || member_id2_str.empty()) {
            callback(createApiResponse(400, "必须提供两个成员ID"));
            return;
        }

        int member_id1, member_id2;
        try {
            member_id1 = std::stoi(member_id1_str);
            member_id2 = std::stoi(member_id2_str);
        } catch (...) {
            callback(createApiResponse(400, "成员ID参数无效"));
            return;
        }

        if (!checkMemberAccess(member_id1, user_id) || !checkMemberAccess(member_id2, user_id)) {
            callback(createApiResponse(403, "无权查看这些成员"));
            return;
        }

        if (member_id1 == member_id2) {
            callback(createApiResponse(400, "不能查询同一个成员"));
            return;
        }

        // 支持 max_depth 参数（默认 20）
        int max_depth = 20;
        auto max_depth_str = req->getParameter("max_depth");
        if (!max_depth_str.empty()) {
            try {
                max_depth = std::stoi(max_depth_str);
                if (max_depth < 1) max_depth = 1;
                if (max_depth > 30) max_depth = 30;
            } catch (...) {
                callback(createApiResponse(400, "最大深度参数无效"));
                return;
            }
        }

        try {
            auto client = app().getDbClient();

            // 获取成员1的所有亲属（包括配偶及其亲属）
            auto relatives1 = getAncestorPathWithSpouse(member_id1, max_depth);
            relatives1.insert(relatives1.begin(), member_id1);

            // 获取成员2的所有亲属（包括配偶及其亲属）
            auto relatives2 = getAncestorPathWithSpouse(member_id2, max_depth);
            relatives2.insert(relatives2.begin(), member_id2);

            // 使用 map 加速查找公共亲属
            std::unordered_map<int, int> relatives2_map;
            for (size_t j = 0; j < relatives2.size(); j++) {
                relatives2_map[relatives2[j]] = static_cast<int>(j);
            }

            // 查找最近公共亲属
            int common_relative = 0;
            int depth1 = 0, depth2 = 0;
            std::string relationship_type = "blood";
            bool path_too_long = false;

            for (size_t i = 0; i < relatives1.size(); i++) {
                auto it = relatives2_map.find(relatives1[i]);
                if (it != relatives2_map.end()) {
                    common_relative = relatives1[i];
                    depth1 = static_cast<int>(i);
                    depth2 = it->second;

                    // 检查是否是配偶关系
                    if (i == 0 && depth2 > 0) {
                        relationship_type = "family";
                    } else if (depth2 == 0 && i > 0) {
                        relationship_type = "family";
                    }
                    break;
                }
            }

            // 如果搜索达到深度限制且未找到公共祖先
            if (common_relative == 0 &&
                (static_cast<int>(relatives1.size()) >= max_depth ||
                 static_cast<int>(relatives2.size()) >= max_depth)) {
                path_too_long = true;
            }

            // ★ 性能优化：收集所有需要查询详情的成员 ID，用一次批量查询替代 N+1 次
            // getMemberDetails() 每次 1 个 SQL，原来最多 40+ 次，现在 1 次
            std::set<int> allMemberIds;
            allMemberIds.insert(member_id1);
            allMemberIds.insert(member_id2);

            if (common_relative > 0) {
                allMemberIds.insert(common_relative);
                // 路径1上的成员
                for (int i = 0; i <= depth1 && i < static_cast<int>(relatives1.size()); i++) {
                    allMemberIds.insert(relatives1[i]);
                }
                // 路径2上的成员
                for (int i = 0; i <= depth2 && i < static_cast<int>(relatives2.size()); i++) {
                    allMemberIds.insert(relatives2[i]);
                }
            }

            // 构建 IN 查询的 ID 列表
            std::string idList;
            for (int id : allMemberIds) {
                if (!idList.empty()) idList += ",";
                idList += std::to_string(id);
            }

            // 批量查询所有成员详情
            std::string batchSql = "SELECT member_id, name, gender, birth_date, death_date,"
                " genealogy_id, father_id, mother_id, spouse_id, generation"
                " FROM Member WHERE member_id IN (" + idList + ")";

            auto batchResult = client->execSqlSync(batchSql);

            // 构建 member_id → detail JSON 的映射
            std::unordered_map<int, Value> detailMap;
            for (const auto& row : batchResult) {
                int mid = row["member_id"].as<int>();

                models::Member member;
                member.member_id = mid;
                member.genealogy_id = row["genealogy_id"].as<int>();
                member.name = row["name"].as<std::string>();
                member.gender = row["gender"].as<std::string>();
                member.birth_date = dbutils::safeStr(row, "birth_date");
                member.death_date = dbutils::safeStr(row, "death_date");
                member.father_id = dbutils::safeInt(row, "father_id");
                member.mother_id = dbutils::safeInt(row, "mother_id");
                member.spouse_id = dbutils::safeInt(row, "spouse_id");
                member.generation = dbutils::safeInt(row, "generation");

                detailMap[mid] = member.toJson();
            }

            // 从批量查询结果获取 spouse_id（无需额外 getSpouseId() 查询）
            auto getSpouseIdFromMap = [&](int mid) -> int {
                auto it = detailMap.find(mid);
                if (it != detailMap.end() && it->second.isMember("spouse_id")) {
                    return it->second["spouse_id"].asInt();
                }
                return 0;
            };

            // 如果是配偶关系（depth1==1 && depth2==1），验证并更新类型
            if (common_relative > 0 && depth1 == 1 && depth2 == 1) {
                int s1 = getSpouseIdFromMap(member_id1);
                int s2 = getSpouseIdFromMap(member_id2);
                if (s1 == member_id2 || s2 == member_id1) {
                    relationship_type = "spouse";
                }
            }

            Value data;
            data["member1_id"] = member_id1;
            data["member2_id"] = member_id2;
            data["has_relationship"] = (common_relative > 0);
            data["max_depth"] = max_depth;
            data["path_too_long"] = path_too_long;

            if (common_relative > 0) {
                data["common_relative_id"] = common_relative;
                data["generation_gap1"] = depth1;
                data["generation_gap2"] = depth2;
                data["relationship_type"] = relationship_type;

                // 从批量查询 map 获取公共亲属详情
                {
                    auto it = detailMap.find(common_relative);
                    if (it != detailMap.end()) data["common_relative"] = it->second;
                }

                // 从批量查询 map 构建路径1
                Value path1(Json::arrayValue);
                for (int i = 0; i <= depth1 && i < static_cast<int>(relatives1.size()); i++) {
                    auto it = detailMap.find(relatives1[i]);
                    if (it != detailMap.end()) path1.append(it->second);
                }
                data["path1"] = path1;

                // 从批量查询 map 构建路径2
                Value path2(Json::arrayValue);
                for (int i = 0; i <= depth2 && i < static_cast<int>(relatives2.size()); i++) {
                    auto it = detailMap.find(relatives2[i]);
                    if (it != detailMap.end()) path2.append(it->second);
                }
                data["path2"] = path2;

                // 如果是配偶关系，从 map 获取配偶详情
                if (relationship_type == "spouse") {
                    int s1 = getSpouseIdFromMap(member_id1);
                    int s2 = getSpouseIdFromMap(member_id2);
                    if (s1 > 0) {
                        auto it = detailMap.find(s1);
                        // 配偶可能不在 allMemberIds 中，需要单独补充查询
                        if (it == detailMap.end()) {
                            auto spResult = client->execSqlSync(
                                "SELECT member_id, name, gender, birth_date, death_date,"
                                " genealogy_id, father_id, mother_id, spouse_id, generation"
                                " FROM Member WHERE member_id = ?", s1);
                            if (!spResult.empty()) {
                                models::Member m;
                                m.member_id = spResult[0]["member_id"].as<int>();
                                m.genealogy_id = spResult[0]["genealogy_id"].as<int>();
                                m.name = spResult[0]["name"].as<std::string>();
                                m.gender = spResult[0]["gender"].as<std::string>();
                                m.birth_date = dbutils::safeStr(spResult[0], "birth_date");
                                m.death_date = dbutils::safeStr(spResult[0], "death_date");
                                m.father_id = dbutils::safeInt(spResult[0], "father_id");
                                m.mother_id = dbutils::safeInt(spResult[0], "mother_id");
                                m.spouse_id = dbutils::safeInt(spResult[0], "spouse_id");
                                m.generation = dbutils::safeInt(spResult[0], "generation");
                                data["spouse1"] = m.toJson();
                            }
                        } else {
                            data["spouse1"] = it->second;
                        }
                    }
                    if (s2 > 0) {
                        auto it = detailMap.find(s2);
                        if (it == detailMap.end()) {
                            auto spResult = client->execSqlSync(
                                "SELECT member_id, name, gender, birth_date, death_date,"
                                " genealogy_id, father_id, mother_id, spouse_id, generation"
                                " FROM Member WHERE member_id = ?", s2);
                            if (!spResult.empty()) {
                                models::Member m;
                                m.member_id = spResult[0]["member_id"].as<int>();
                                m.genealogy_id = spResult[0]["genealogy_id"].as<int>();
                                m.name = spResult[0]["name"].as<std::string>();
                                m.gender = spResult[0]["gender"].as<std::string>();
                                m.birth_date = dbutils::safeStr(spResult[0], "birth_date");
                                m.death_date = dbutils::safeStr(spResult[0], "death_date");
                                m.father_id = dbutils::safeInt(spResult[0], "father_id");
                                m.mother_id = dbutils::safeInt(spResult[0], "mother_id");
                                m.spouse_id = dbutils::safeInt(spResult[0], "spouse_id");
                                m.generation = dbutils::safeInt(spResult[0], "generation");
                                data["spouse2"] = m.toJson();
                            }
                        } else {
                            data["spouse2"] = it->second;
                        }
                    }
                }
            }

            callback(createApiResponse(200, "查询成功", data));

        } catch (const std::exception& e) {
            std::cerr << "查询亲缘关系错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "查询亲缘关系异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// 获取祖先路径（线性链，优先父系，追溯到根节点）
// ============================================================
// 获取祖先线性路径（优先父系，追溯到根节点）
// ★ 优化：使用 ancestor_path 解析替代 while 循环 N+1 查询
// 格式：/1/862/2717/.../33332 → 解析得到 [1, 862, 2717, ..., 33332]
// ============================================================
std::vector<int> RelationshipController::getAncestorLineagePath(int member_id) {
    std::vector<int> path;
    auto client = app().getDbClient();

    try {
        // 一次查询获取 ancestor_path
        auto result = client->execSqlSync(
            "SELECT ancestor_path FROM Member WHERE member_id = ?",
            member_id
        );

        if (result.empty()) return path;

        std::string ancestor_path = dbutils::safeStr(result[0], "ancestor_path");
        if (ancestor_path.empty() || ancestor_path[0] != '/') return path;

        // 解析 ancestor_path，排除目标成员自身
        size_t start = 1;
        while (start < ancestor_path.length()) {
            size_t end = ancestor_path.find('/', start);
            if (end == std::string::npos) end = ancestor_path.length();
            std::string id_str = ancestor_path.substr(start, end - start);
            if (!id_str.empty()) {
                int id = std::stoi(id_str);
                if (id != member_id) path.push_back(id);
            }
            start = end + 1;
        }

        // 路径已是根→成员顺序（自然顺序），直接返回
        return path;

    } catch (const std::exception& e) {
        std::cerr << "获取祖先路径异常: " << e.what() << std::endl;
        return path;
    }
}

// ============================================================
// 获取家族树接口实现（性能优化版）
// ============================================================
// 优化要点：
//   1. 移除 depth=20 硬上限 → 最大 100，0=不限
//   2. 祖先查找用 ancestor_path 解析（1 条 SQL），替代 N+1 while 循环
//   3. 后代查找复用 getDescendantPath（已有根节点快速通道）
//   4. 消除 N+1 getSpouseId()：先从批量查询结果提取 spouse_id，一次性补查
//   5. 精确列选择替代 SELECT *
// ============================================================
void RelationshipController::getFamilyTree(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback,
                                         int member_id) {
    try {
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }

        int user_id = getUserIdFromToken(token);
        if (user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }

        if (!checkMemberAccess(member_id, user_id)) {
            callback(createApiResponse(403, "无权查看此成员"));
            return;
        }

        // depth 控制后代深度，0=不限（与 getDescendants 一致），最大 100
        int depth = 5;
        auto depth_str = req->getParameter("depth");
        if (!depth_str.empty()) {
            try {
                depth = std::stoi(depth_str);
                if (depth < 0) depth = 0;
                if (depth > 100) depth = 100;
            } catch (...) {
                callback(createApiResponse(400, "深度参数无效"));
                return;
            }
        }

        auto client = app().getDbClient();

        // ============================================================
        // 步骤1：获取目标成员的 ancestor_path + 基本信息（1 条 SQL）
        // ============================================================
        auto targetResult = client->execSqlSync(
            "SELECT member_id, name, gender, birth_date, death_date, biography,"
            " genealogy_id, father_id, mother_id, spouse_id, generation, ancestor_path"
            " FROM Member WHERE member_id = ?", member_id);

        if (targetResult.empty()) {
            callback(createApiResponse(404, "成员不存在"));
            return;
        }

        std::string ancestor_path = dbutils::safeStr(targetResult[0], "ancestor_path");

        // 构建 target_member（从查询结果直接构建，省一次 getMemberDetails 调用）
        Value target_member;
        target_member["member_id"] = member_id;
        target_member["name"] = targetResult[0]["name"].as<std::string>();
        target_member["gender"] = targetResult[0]["gender"].as<std::string>();
        target_member["genealogy_id"] = targetResult[0]["genealogy_id"].as<int>();
        target_member["father_id"] = dbutils::safeInt(targetResult[0], "father_id");
        target_member["mother_id"] = dbutils::safeInt(targetResult[0], "mother_id");
        target_member["spouse_id"] = dbutils::safeInt(targetResult[0], "spouse_id");
        target_member["generation"] = dbutils::safeInt(targetResult[0], "generation");
        std::string bdate = dbutils::safeStr(targetResult[0], "birth_date");
        std::string ddate = dbutils::safeStr(targetResult[0], "death_date");
        std::string bio = dbutils::safeStr(targetResult[0], "biography");
        if (!bdate.empty()) target_member["birth_date"] = bdate;
        if (!ddate.empty()) target_member["death_date"] = ddate;
        if (!bio.empty()) target_member["biography"] = bio;

        // ============================================================
        // 步骤2：从 ancestor_path 解析祖先 ID 列表（无需 SQL）
        // 格式：/1/862/2717/.../33332，排除最后一个（目标成员自身）
        // ============================================================
        std::vector<int> ancestors;
        if (!ancestor_path.empty() && ancestor_path[0] == '/') {
            size_t start = 1;
            while (start < ancestor_path.length()) {
                size_t end = ancestor_path.find('/', start);
                if (end == std::string::npos) end = ancestor_path.length();
                std::string id_str = ancestor_path.substr(start, end - start);
                if (!id_str.empty()) {
                    int id = std::stoi(id_str);
                    if (id != member_id) ancestors.push_back(id);
                }
                start = end + 1;
            }
        }
        // 反转：成员→根（近→远）
        std::reverse(ancestors.begin(), ancestors.end());

        // ============================================================
        // 步骤3：获取后代 ID 列表（复用已优化的 getDescendantPath）
        // ============================================================
        auto descendants = getDescendantPath(member_id, depth);

        // ============================================================
        // 步骤4：收集所有需要查询的 ID（不含配偶，配偶从批量结果中提取）
        // ============================================================
        std::unordered_set<int> allIds;
        allIds.insert(member_id);
        for (int id : ancestors) allIds.insert(id);
        for (int id : descendants) allIds.insert(id);

        // ============================================================
        // 步骤5：批量查询所有成员 + 配偶（精确列，无 SELECT *）
        // ============================================================
        std::unordered_map<int, Json::Value> memberCache;

        // 第一轮：查询已收集的 ID
        if (!allIds.empty()) {
            std::string idList;
            for (int id : allIds) {
                if (!idList.empty()) idList += ",";
                idList += std::to_string(id);
            }

            auto batchResult = client->execSqlSync(
                "SELECT member_id, name, gender, birth_date, death_date,"
                " genealogy_id, father_id, mother_id, spouse_id, generation"
                " FROM Member WHERE member_id IN (" + idList + ")");

            std::set<int> missingSpouseIds;
            for (const auto& row : batchResult) {
                int mid = row["member_id"].as<int>();
                Value m;
                m["member_id"] = mid;
                m["name"] = row["name"].as<std::string>();
                m["gender"] = row["gender"].as<std::string>();
                m["genealogy_id"] = row["genealogy_id"].as<int>();
                m["father_id"] = dbutils::safeInt(row, "father_id");
                m["mother_id"] = dbutils::safeInt(row, "mother_id");
                m["spouse_id"] = dbutils::safeInt(row, "spouse_id");
                m["generation"] = dbutils::safeInt(row, "generation");
                std::string bd = dbutils::safeStr(row, "birth_date");
                std::string dd = dbutils::safeStr(row, "death_date");
                if (!bd.empty()) m["birth_date"] = bd;
                if (!dd.empty()) m["death_date"] = dd;
                memberCache[mid] = m;

                int sid = dbutils::safeInt(row, "spouse_id");
                if (sid > 0 && allIds.find(sid) == allIds.end()) {
                    missingSpouseIds.insert(sid);
                }
            }

            // 第二轮：补查缺失的配偶（通常很少）
            if (!missingSpouseIds.empty()) {
                std::string spouseIdList;
                for (int sid : missingSpouseIds) {
                    if (!spouseIdList.empty()) spouseIdList += ",";
                    spouseIdList += std::to_string(sid);
                }
                auto spouseResult = client->execSqlSync(
                    "SELECT member_id, name, gender, birth_date, death_date,"
                    " genealogy_id, father_id, mother_id, spouse_id, generation"
                    " FROM Member WHERE member_id IN (" + spouseIdList + ")");

                for (const auto& row : spouseResult) {
                    int mid = row["member_id"].as<int>();
                    Value m;
                    m["member_id"] = mid;
                    m["name"] = row["name"].as<std::string>();
                    m["gender"] = row["gender"].as<std::string>();
                    m["genealogy_id"] = row["genealogy_id"].as<int>();
                    m["father_id"] = dbutils::safeInt(row, "father_id");
                    m["mother_id"] = dbutils::safeInt(row, "mother_id");
                    m["spouse_id"] = dbutils::safeInt(row, "spouse_id");
                    m["generation"] = dbutils::safeInt(row, "generation");
                    std::string bd = dbutils::safeStr(row, "birth_date");
                    std::string dd = dbutils::safeStr(row, "death_date");
                    if (!bd.empty()) m["birth_date"] = bd;
                    if (!dd.empty()) m["death_date"] = dd;
                    memberCache[mid] = m;
                }
            }
        }

        // ============================================================
        // 步骤6：挂接 target_member 的配偶
        // ============================================================
        int target_spouse = dbutils::safeInt(targetResult[0], "spouse_id");
        if (target_spouse > 0) {
            auto it = memberCache.find(target_spouse);
            if (it != memberCache.end()) {
                target_member["spouse"] = it->second;
            }
        }

        // ============================================================
        // 步骤7：构建祖先数组（从根→父/母的顺序，挂接配偶）
        // ============================================================
        Value ancestorsArray(Json::arrayValue);
        for (int ancestor_id : ancestors) {
            auto it = memberCache.find(ancestor_id);
            if (it != memberCache.end()) {
                Value ancestor = it->second;
                int asp = 0;
                if (ancestor.isMember("spouse_id")) asp = ancestor["spouse_id"].asInt();
                if (asp > 0) {
                    auto sit = memberCache.find(asp);
                    if (sit != memberCache.end()) {
                        ancestor["spouse"] = sit->second;
                    }
                }
                ancestorsArray.append(ancestor);
            }
        }

        // ============================================================
        // 步骤8：构建后代数组（挂接配偶）
        // ============================================================
        Value descendantsArray(Json::arrayValue);
        for (int descendant_id : descendants) {
            auto it = memberCache.find(descendant_id);
            if (it != memberCache.end()) {
                Value descendant = it->second;
                int dsp = 0;
                if (descendant.isMember("spouse_id")) dsp = descendant["spouse_id"].asInt();
                if (dsp > 0) {
                    auto sit = memberCache.find(dsp);
                    if (sit != memberCache.end()) {
                        descendant["spouse"] = sit->second;
                    }
                }
                descendantsArray.append(descendant);
            }
        }

        Value data;
        data["target_member"] = target_member;
        data["depth"] = depth;
        data["ancestors"] = ancestorsArray;
        data["descendants"] = descendantsArray;
        data["total_ancestors"] = static_cast<int>(ancestorsArray.size());
        data["total_descendants"] = static_cast<int>(descendantsArray.size());

        callback(createApiResponse(200, "获取成功", data));

    } catch (const std::exception& e) {
        std::cerr << "获取家族树异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// ============================================================
// 分层树加载接口：GET /api/genealogy/{gid}/tree-layers
// 支持按层返回家族树数据，替代 getAllMembers 全量加载
// 参数：root_id, max_depth, layer_page, layer_page_size
// ============================================================
void RelationshipController::getTreeLayers(const drogon::HttpRequestPtr& req,
                                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                            int genealogy_id) {
    try {
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }

        int user_id = getUserIdFromToken(token);
        if (user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }

        // 验证族谱权限（检查是否是 owner 或有共享权限）
        auto client = app().getDbClient();
        try {
            auto checkResult = client->execSqlSync(
                "SELECT user_id FROM Genealogy WHERE genealogy_id = ?",
                genealogy_id
            );
            if (checkResult.empty()) {
                callback(createApiResponse(404, "族谱不存在"));
                return;
            }
            int owner_id = checkResult[0]["user_id"].as<int>();
            if (owner_id != user_id) {
                auto shareResult = client->execSqlSync(
                    "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
                    genealogy_id, user_id
                );
                if (shareResult.empty()) {
                    callback(createApiResponse(403, "无权查看此族谱"));
                    return;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "验证族谱权限错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "权限验证失败"));
            return;
        }

        // 解析参数
        int max_depth = 5;
        auto max_depth_str = req->getParameter("max_depth");
        if (!max_depth_str.empty()) {
            try {
                max_depth = std::stoi(max_depth_str);
                if (max_depth < 1) max_depth = 1;
                if (max_depth > 10) max_depth = 10;
            } catch (...) {}
        }

        int layer_page_size = 500;
        auto lps_str = req->getParameter("layer_page_size");
        if (!lps_str.empty()) {
            try {
                layer_page_size = std::stoi(lps_str);
                if (layer_page_size < 1) layer_page_size = 500;
                if (layer_page_size > 1000) layer_page_size = 1000;
            } catch (...) {}
        }

        int layer_page = 1;
        auto lp_str = req->getParameter("layer_page");
        if (!lp_str.empty()) {
            try {
                layer_page = std::stoi(lp_str);
                if (layer_page < 1) layer_page = 1;
            } catch (...) {}
        }

        try {
            // 1. 获取根节点（最小 generation 的成员）
            int root_id = 0;
            auto root_id_str = req->getParameter("root_id");
            if (!root_id_str.empty()) {
                try { root_id = std::stoi(root_id_str); } catch (...) {}
            }

            Value rootIds(Json::arrayValue);
            if (root_id > 0) {
                // 指定根节点
                auto checkRoot = client->execSqlSync(
                    "SELECT member_id FROM Member WHERE member_id = ? AND genealogy_id = ?",
                    root_id, genealogy_id
                );
                if (checkRoot.empty()) {
                    callback(createApiResponse(400, "指定的根节点不属于此族谱"));
                    return;
                }
                rootIds.append(root_id);
            } else {
                // 自动获取最小 generation 的所有成员作为根节点
                auto minGenResult = client->execSqlSync(R"(
                    SELECT MIN(generation) as min_gen FROM Member
                    WHERE genealogy_id = ? AND generation IS NOT NULL AND generation > 0
                )", genealogy_id);

                int min_gen = 1;
                if (!minGenResult.empty() && !minGenResult[0]["min_gen"].isNull()) {
                    min_gen = minGenResult[0]["min_gen"].as<int>();
                }

                auto rootsResult = client->execSqlSync(
                    "SELECT member_id FROM Member WHERE genealogy_id = ? AND generation = ? ORDER BY member_id LIMIT 10",
                    genealogy_id, min_gen
                );

                for (const auto& row : rootsResult) {
                    rootIds.append(row["member_id"].as<int>());
                }
            }

            // 2. 获取总节点数
            auto totalResult = client->execSqlSync(
                "SELECT COUNT(*) as total FROM Member WHERE genealogy_id = ?",
                genealogy_id
            );
            int total_nodes = totalResult.empty() ? 0 : totalResult[0]["total"].as<int>();

            // 3. 使用递归 CTE 获取分层数据（从根节点向下展开到 max_depth 层）
            if (rootIds.empty()) {
                Value data;
                data["root_ids"] = rootIds;
                data["total_nodes"] = total_nodes;
                data["returned_nodes"] = 0;
                data["has_more"] = false;
                data["max_depth_reached"] = 0;
                data["layers"] = Json::objectValue;
                callback(createApiResponse(200, "族谱为空", data));
                return;
            }

            // 构建 IN 子句的占位符
            std::string placeholders;
            for (Json::ArrayIndex i = 0; i < rootIds.size(); i++) {
                if (i > 0) placeholders += ",";
                placeholders += std::to_string(rootIds[i].asInt());
            }

            std::string treeSql = R"(
WITH RECURSIVE tree AS (
    SELECT member_id, name, gender, birth_date, death_date,
           genealogy_id, father_id, mother_id, spouse_id, generation,
           0 as depth
    FROM Member
    WHERE member_id IN ()" + placeholders + R"()
      AND genealogy_id = )" + std::to_string(genealogy_id) + R"(

    UNION ALL

    SELECT m.member_id, m.name, m.gender, m.birth_date, m.death_date,
           m.genealogy_id, m.father_id, m.mother_id, m.spouse_id, m.generation,
           t.depth + 1
    FROM Member m
    JOIN tree t ON (m.father_id = t.member_id OR m.mother_id = t.member_id)
    WHERE m.genealogy_id = )" + std::to_string(genealogy_id) + R"(
      AND t.depth < )" + std::to_string(max_depth) + R"(
)
SELECT * FROM tree ORDER BY depth, generation, birth_date LIMIT 2000
)";

            auto treeResult = client->execSqlSync(treeSql);

            // 4. 按层组织数据
            std::map<int, Json::Value> layersMap;  // depth -> array of members
            int max_depth_reached = 0;
            int returned_nodes = 0;

            for (const auto& row : treeResult) {
                int depth = row["depth"].as<int>();
                if (depth > max_depth_reached) max_depth_reached = depth;

                models::Member member;
                member.member_id = row["member_id"].as<int>();
                member.genealogy_id = row["genealogy_id"].as<int>();
                member.name = row["name"].as<std::string>();
                member.gender = row["gender"].as<std::string>();
                member.birth_date = dbutils::safeStr(row, "birth_date");
                member.death_date = dbutils::safeStr(row, "death_date");
                member.father_id = dbutils::safeInt(row, "father_id");
                member.mother_id = dbutils::safeInt(row, "mother_id");
                member.spouse_id = dbutils::safeInt(row, "spouse_id");
                member.generation = dbutils::safeInt(row, "generation");

                Value node = member.toJson();
                node["depth"] = depth;
                layersMap[depth].append(node);
                returned_nodes++;
            }

            // 5. 构建响应
            Value layers(Json::objectValue);
            for (const auto& [depth, nodes] : layersMap) {
                layers[std::to_string(depth)] = nodes;
            }

            Value data;
            data["root_ids"] = rootIds;
            data["total_nodes"] = total_nodes;
            data["returned_nodes"] = returned_nodes;
            data["has_more"] = (returned_nodes >= 2000) || (max_depth_reached >= max_depth);
            data["max_depth_reached"] = max_depth_reached;
            data["max_depth"] = max_depth;
            data["layers"] = layers;

            callback(createApiResponse(200, "获取成功", data));

        } catch (const std::exception& e) {
            std::cerr << "分层树加载错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "分层树加载异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}
