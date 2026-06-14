// controllers/MemberController.cc
#include "MemberController.h"
#include "AuthController.h"
#include "ResponseUtils.h"
#include "models/Member.h"
#include "models/Genealogy.h"
#include "models/DbUtils.h"
#include "models/StatsCache.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>
#include <regex>
#include <iomanip>
#include <sstream>
#include <unordered_map>

using namespace api::v1;
using namespace drogon;
using Json::Value;
using myutils::createApiResponse;

// 从请求头获取token
std::string MemberController::getTokenFromHeader(const HttpRequestPtr& req) {
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0) {
        return authHeader.substr(7);
    }
    
    auto tokenHeader = req->getHeader("X-Token");
    if (!tokenHeader.empty()) {
        return tokenHeader;
    }
    
    return "";
}

int MemberController::getUserIdFromToken(const std::string& token) {
    if (token.empty()) {
        return 0;
    }
    
    auto [userId, username] = AuthController::validateToken(token);
    return userId;
}

// 验证用户对族谱的权限
bool MemberController::checkGenealogyAccess(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        // 1. 检查是否是创建者
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!result.empty()) {
            return true;  // 创建者有完全权限
        }
        
        // 2. 检查是否在共享列表中
        auto shareResult = client->execSqlSync(
            "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!shareResult.empty()) {
            std::string permission = shareResult[0]["permission"].as<std::string>();
            return permission == "edit";  // 只有有编辑权限的共享用户才能操作成员
        }
        
        return false;
    } catch (const std::exception& e) {
        std::cerr << "检查族谱权限错误: " << e.what() << std::endl;
        return false;
    }
}

// 验证用户对成员的权限
bool MemberController::checkMemberAccess(int member_id, int user_id) {
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
            return owner_id == user_id;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "检查成员权限错误: " << e.what() << std::endl;
        return false;
    }
}

// 验证性别格式
bool MemberController::isValidGender(const std::string& gender) {
    return gender == "male" || gender == "female";
}

// 验证日期格式 (YYYY-MM-DD)
bool MemberController::isValidDate(const std::string& date_str) {
    if (date_str.empty()) return true;  // 允许为空
    
    std::regex pattern("^\\d{4}-\\d{2}-\\d{2}$");
    if (!std::regex_match(date_str, pattern)) {
        return false;
    }
    
    // 简单的日期验证
    std::istringstream ss(date_str);
    int year, month, day;
    char dash1, dash2;
    
    if (!(ss >> year >> dash1 >> month >> dash2 >> day) || 
        dash1 != '-' || dash2 != '-') {
        return false;
    }
    
    if (year < 1000 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    
    return true;
}

// 验证父母ID是否存在且属于同一族谱
bool MemberController::validateParentIds(int genealogy_id, int father_id, int mother_id) {
    auto client = app().getDbClient();
    
    try {
        if (father_id > 0) {
            auto result = client->execSqlSync(
                "SELECT member_id FROM Member WHERE member_id = ? AND genealogy_id = ? AND gender = 'male'",
                father_id, genealogy_id
            );
            if (result.empty()) {
                return false;  // 父亲不存在或不属于同一族谱
            }
        }
        
        if (mother_id > 0) {
            auto result = client->execSqlSync(
                "SELECT member_id FROM Member WHERE member_id = ? AND genealogy_id = ? AND gender = 'female'",
                mother_id, genealogy_id
            );
            if (result.empty()) {
                return false;  // 母亲不存在或不属于同一族谱
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "验证父母ID错误: " << e.what() << std::endl;
        return false;
    }
}

// 验证成员是否能被删除
bool MemberController::canDeleteMember(int member_id) {
    auto client = app().getDbClient();
    try {
        // 检查是否有后代
        auto result = client->execSqlSync(
            "SELECT COUNT(*) as count FROM Member WHERE father_id = ? OR mother_id = ?",
            member_id, member_id
        );
        
        int descendantCount = result[0]["count"].as<int>();
        if (descendantCount > 0) {
            return false;  // 有后代，不能删除
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "检查成员删除权限错误: " << e.what() << std::endl;
        return false;
    }
}

void MemberController::createMember(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  int genealogy_id) {
    try {
        // 1. 验证token
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
        
        // 2. 验证对族谱的权限
        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权在此族谱添加成员"));
            return;
        }
        
        // 3. 解析请求体
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }
        
        // 4. 验证必填字段
        if (!json->isMember("name") || !json->isMember("gender")) {
            callback(createApiResponse(400, "姓名和性别为必填项"));
            return;
        }
        
        std::string name = (*json)["name"].asString();
        std::string gender = (*json)["gender"].asString();
        std::string birth_date = json->isMember("birth_date") ? (*json)["birth_date"].asString() : "";
        std::string death_date = json->isMember("death_date") ? (*json)["death_date"].asString() : "";
        std::string biography = json->isMember("biography") ? (*json)["biography"].asString() : "";
        int father_id = json->isMember("father_id") ? (*json)["father_id"].asInt() : 0;
        int mother_id = json->isMember("mother_id") ? (*json)["mother_id"].asInt() : 0;
        int spouse_id = json->isMember("spouse_id") ? (*json)["spouse_id"].asInt() : 0;  // 新增：配偶ID
        
        // 5. 验证数据格式
        if (name.empty()) {
            callback(createApiResponse(400, "姓名不能为空"));
            return;
        }
        
        if (!isValidGender(gender)) {
            callback(createApiResponse(400, "性别必须是 male 或 female"));
            return;
        }
        
        if (!isValidDate(birth_date)) {
            callback(createApiResponse(400, "出生日期格式无效 (YYYY-MM-DD)"));
            return;
        }
        
        if (!isValidDate(death_date)) {
            callback(createApiResponse(400, "逝世日期格式无效 (YYYY-MM-DD)"));
            return;
        }
        
        // 6. 验证父母ID
        if (!validateParentIds(genealogy_id, father_id, mother_id)) {
            callback(createApiResponse(400, "父母ID无效或不属于同一族谱"));
            return;
        }
        
        // 7. 验证配偶ID（新增）
        auto client = app().getDbClient();
        
        if (spouse_id > 0) {
            try {
                // 验证配偶存在且属于同一族谱
                auto spouseResult = client->execSqlSync(
                    "SELECT member_id, gender, spouse_id FROM Member WHERE member_id = ? AND genealogy_id = ?",
                    spouse_id, genealogy_id
                );
                
                if (spouseResult.empty()) {
                    callback(createApiResponse(400, "配偶不存在或不属于同一族谱"));
                    return;
                }
                
                // 验证性别：必须为异性
                std::string spouse_gender = spouseResult[0]["gender"].as<std::string>();
                if (gender == spouse_gender) {
                    callback(createApiResponse(400, "配偶必须是异性"));
                    return;
                }
                
                // 验证配偶是否已有配偶
                int existing_spouse_id = spouseResult[0]["spouse_id"].as<int>();
                if (existing_spouse_id > 0) {
                    callback(createApiResponse(400, "该成员已有配偶"));
                    return;
                }
                
            } catch (const std::exception& e) {
                std::cerr << "验证配偶错误: " << e.what() << std::endl;
                callback(createApiResponse(500, "验证配偶失败"));
                return;
            }
        }
        
        // 8. 计算辈分（generation）
        int generation = 1;  // 默认：没有父辈信息则为始祖（第1代）
        if (father_id > 0 || mother_id > 0) {
            try {
                // 从父辈中获取辈分最大的值
                // ★ 修复：如果父辈 generation 为 NULL，按 1 处理（确保子代永远比父代至少大 1 代）
                int parent_gen = 1;  // ★ 修复：从 1 开始而非 0，确保 NULL generation 不导致辈分扁平
                if (father_id > 0) {
                    auto fg = client->execSqlSync(
                        "SELECT generation FROM Member WHERE member_id = ?", father_id);
                    if (!fg.empty() && !fg[0]["generation"].isNull()) {
                        int fgen = fg[0]["generation"].as<int>();
                        if (fgen > parent_gen) parent_gen = fgen;
                    }
                }
                if (mother_id > 0) {
                    auto mg = client->execSqlSync(
                        "SELECT generation FROM Member WHERE member_id = ?", mother_id);
                    if (!mg.empty() && !mg[0]["generation"].isNull()) {
                        int mgen = mg[0]["generation"].as<int>();
                        if (mgen > parent_gen) parent_gen = mgen;
                    }
                }
                generation = parent_gen + 1;  // ★ 修复：始终计算，不依赖 parent_gen > 0 守卫
            } catch (...) {
                generation = 1;
            }
        }

        // 9. 插入数据库
        try {
            // 使用事务确保数据一致性
            client->execSqlSync("START TRANSACTION");

            // 插入成员记录（包含 generation）
            client->execSqlSync(
                "INSERT INTO Member (genealogy_id, name, gender, birth_date, death_date, biography, father_id, mother_id, spouse_id, generation) "
                "VALUES (?, ?, ?, NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, ''), NULLIF(?, 0), NULLIF(?, 0), NULLIF(?, 0), ?)",
                genealogy_id, name, gender,
                birth_date, death_date, biography,
                father_id, mother_id, spouse_id, generation
            );
            
            // 获取新创建的成员ID
            auto result = client->execSqlSync(
                "SELECT member_id FROM Member WHERE genealogy_id = ? AND name = ? ORDER BY member_id DESC LIMIT 1",
                genealogy_id, name
            );
            
            if (result.empty()) {
                client->execSqlSync("ROLLBACK");
                callback(createApiResponse(500, "添加失败，无法获取成员ID"));
                return;
            }
            
            int new_member_id = result[0]["member_id"].as<int>();

            // Phase 3: Materialized Path - 计算 ancestor_path
            // 格式: "/root_id/.../parent_id/member_id"
            try {
                std::string ancestor_path;
                int parent_id = (father_id > 0) ? father_id : mother_id;
                if (parent_id > 0) {
                    auto parentPathResult = client->execSqlSync(
                        "SELECT ancestor_path FROM Member WHERE member_id = ?", parent_id
                    );
                    if (!parentPathResult.empty() && !parentPathResult[0]["ancestor_path"].isNull()) {
                        std::string parent_path = parentPathResult[0]["ancestor_path"].as<std::string>();
                        ancestor_path = parent_path + "/" + std::to_string(new_member_id);
                    } else {
                        // 父辈无 ancestor_path（旧数据），回退：/parent_id/member_id
                        ancestor_path = "/" + std::to_string(parent_id) + "/" + std::to_string(new_member_id);
                    }
                } else {
                    // 始祖
                    ancestor_path = "/" + std::to_string(new_member_id);
                }
                client->execSqlSync(
                    "UPDATE Member SET ancestor_path = ? WHERE member_id = ?",
                    ancestor_path, new_member_id
                );
            } catch (const std::exception& e) {
                // ancestor_path 更新失败不影响主流程
                std::cerr << "更新 ancestor_path 警告: " << e.what() << std::endl;
            }

            // 如果指定了配偶，更新配偶的spouse_id
            if (spouse_id > 0) {
                // 更新配偶的spouse_id
                client->execSqlSync(
                    "UPDATE Member SET spouse_id = ? WHERE member_id = ?",
                    new_member_id, spouse_id
                );
                
                // 在Marriage表中记录婚姻关系
                int husband_id = (gender == "male") ? new_member_id : spouse_id;
                int wife_id = (gender == "female") ? new_member_id : spouse_id;
                
                client->execSqlSync(
                    "INSERT INTO Marriage (husband_id, wife_id, status) VALUES (?, ?, 'active')",
                    husband_id, wife_id
                );
            }
            
            // 提交事务
            client->execSqlSync("COMMIT");
            
            // 返回创建的成员信息
            models::Member member;
            member.member_id = new_member_id;
            member.genealogy_id = genealogy_id;
            member.name = name;
            member.gender = gender;
            member.birth_date = birth_date;
            member.death_date = death_date;
            member.biography = biography;
            member.father_id = father_id;
            member.mother_id = mother_id;
            member.spouse_id = spouse_id;
            member.generation = generation;

            // 如果指定了配偶，获取配偶详情
            Value data = member.toJson();
            if (spouse_id > 0) {
                auto spouseResult = client->execSqlSync(
                    "SELECT member_id, name, gender, birth_date FROM Member WHERE member_id = ?",
                    spouse_id
                );
                if (!spouseResult.empty()) {
                    models::Member spouse;
                    spouse.member_id = spouseResult[0]["member_id"].as<int>();
                    spouse.name = spouseResult[0]["name"].as<std::string>();
                    spouse.gender = spouseResult[0]["gender"].as<std::string>();
                    spouse.birth_date = dbutils::safeStr(spouseResult[0], "birth_date");

                    data["spouse"] = spouse.toJson();
                }
            }

            // Phase 3: 数据变更时失效统计缓存
            cache::StatsCache::instance().invalidatePrefix("dashboard_" + std::to_string(genealogy_id));
            cache::StatsCache::instance().invalidatePrefix("stats_" + std::to_string(genealogy_id));

            callback(createApiResponse(201, "添加成功", data));
            return;
            
        } catch (const std::exception& e) {
            // 回滚事务
            try { client->execSqlSync("ROLLBACK"); } catch (...) {}
            
            std::cerr << "添加成员错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "添加失败: " + std::string(e.what())));
            return;
        }
        
    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
        return;
    } catch (const std::exception& e) {
        std::cerr << "添加成员异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误: " + std::string(e.what())));
        return;
    }
}

void MemberController::getMemberList(const HttpRequestPtr& req,
                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                   int genealogy_id) {
    try {
        // 1. 验证token
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
        
        // 2. 验证对族谱的权限
        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权查看此族谱成员"));
            return;
        }
        
        // 3. 获取分页参数
        int page = 1;
        int page_size = 20;
        
        auto page_str = req->getParameter("page");
        if (!page_str.empty()) {
            try {
                page = std::stoi(page_str);
                if (page < 1) page = 1;
            } catch (...) {
                callback(createApiResponse(400, "页码参数无效"));
                return;
            }
        }
        
        auto page_size_str = req->getParameter("page_size");
        if (!page_size_str.empty()) {
            try {
                page_size = std::stoi(page_size_str);
                if (page_size < 1) page_size = 20;
                if (page_size > 500) page_size = 500;  // 限制最大500条（前端族谱树加载）
            } catch (...) {
                callback(createApiResponse(400, "每页数量参数无效"));
                return;
            }
        }
        
        int offset = (page - 1) * page_size;
        
        // 4. 查询数据库
        auto client = app().getDbClient();
        try {
            // 查询总数
            auto countResult = client->execSqlSync(
                "SELECT COUNT(*) as total FROM Member WHERE genealogy_id = ?",
                genealogy_id
            );
            
            int total = 0;
            if (!countResult.empty()) {
                total = countResult[0]["total"].as<int>();
            }
            
            // 查询成员列表（LEFT JOIN 一次取出配偶信息，消除 N+1）
            auto result = client->execSqlSync(R"(
                SELECT m.*,
                       s.member_id as spouse_member_id, s.name as spouse_name,
                       s.gender as spouse_gender, s.birth_date as spouse_birth_date,
                       s.death_date as spouse_death_date, s.generation as spouse_generation
                FROM Member m
                LEFT JOIN Member s ON m.spouse_id = s.member_id
                WHERE m.genealogy_id = ?
                ORDER BY m.member_id DESC LIMIT ? OFFSET ?
            )", genealogy_id, page_size, offset);
            
            Value data;
            Value members(Json::arrayValue);
            
            for (const auto& row : result) {
                models::Member member;
                member.member_id = row["member_id"].as<int>();
                member.genealogy_id = row["genealogy_id"].as<int>();
                member.name = row["name"].as<std::string>();
                member.gender = row["gender"].as<std::string>();
                member.birth_date = dbutils::safeStr(row, "birth_date");
                member.death_date = dbutils::safeStr(row, "death_date");
                member.biography = dbutils::safeStr(row, "biography");
                member.father_id = dbutils::safeInt(row, "father_id");
                member.mother_id = dbutils::safeInt(row, "mother_id");
                member.spouse_id = dbutils::safeInt(row, "spouse_id");
                member.generation = dbutils::safeInt(row, "generation");

                Value memberJson = member.toJson();

                // 使用LEFT JOIN结果添加配偶详情（消除N+1）
                if (!row["spouse_member_id"].isNull()) {
                    models::Member spouse;
                    spouse.member_id = dbutils::safeInt(row, "spouse_member_id");
                    spouse.name = dbutils::safeStr(row, "spouse_name");
                    spouse.gender = dbutils::safeStr(row, "spouse_gender");
                    spouse.birth_date = dbutils::safeStr(row, "spouse_birth_date");
                    spouse.death_date = dbutils::safeStr(row, "spouse_death_date");
                    spouse.generation = dbutils::safeInt(row, "spouse_generation");

                    memberJson["spouse"] = spouse.toJson();
                }

                members.append(memberJson);
            }

            data["members"] = members;
            data["total"] = total;
            data["page"] = page;
            data["page_size"] = page_size;
            data["total_pages"] = (total + page_size - 1) / page_size;

            auto resp = createApiResponse(200, "获取成功", data);
            resp->addHeader("Cache-Control", "public, max-age=30");
            callback(resp);

        } catch (const std::exception& e) {
            std::cerr << "查询成员列表错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败"));
        }

    } catch (const std::exception& e) {
        std::cerr << "获取成员列表异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}


void MemberController::getMemberDetail(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     int member_id) {
    try {
        // 1. 验证token
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
        
        // 2. 验证对成员的权限
        if (!checkMemberAccess(member_id, user_id)) {
            callback(createApiResponse(403, "无权查看此成员"));
            return;
        }
        
        // 3. 查询数据库
        auto client = app().getDbClient();
        try {
            // 返回成员详情（LEFT JOIN 一次取出配偶信息）
            auto result = client->execSqlSync(R"(
                SELECT m.*,
                       s.member_id as spouse_member_id, s.name as spouse_name,
                       s.gender as spouse_gender, s.birth_date as spouse_birth_date,
                       s.death_date as spouse_death_date, s.generation as spouse_generation,
                       s.genealogy_id as spouse_genealogy_id
                FROM Member m
                LEFT JOIN Member s ON m.spouse_id = s.member_id
                WHERE m.member_id = ?
            )", member_id);
            
            if (result.empty()) {
                callback(createApiResponse(404, "成员不存在"));
                return;
            }
            
            // 返回成员详情（包含 generation 和配偶 LEFT JOIN 结果）
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

            Value data = member.toJson();

            // 使用 LEFT JOIN 结果添加配偶详情（消除 N+1）
            if (!result[0]["spouse_member_id"].isNull()) {
                models::Member spouse;
                spouse.member_id = dbutils::safeInt(result[0], "spouse_member_id");
                spouse.genealogy_id = dbutils::safeInt(result[0], "spouse_genealogy_id");
                spouse.name = dbutils::safeStr(result[0], "spouse_name");
                spouse.gender = dbutils::safeStr(result[0], "spouse_gender");
                spouse.birth_date = dbutils::safeStr(result[0], "spouse_birth_date");
                spouse.death_date = dbutils::safeStr(result[0], "spouse_death_date");
                spouse.generation = dbutils::safeInt(result[0], "spouse_generation");

                data["spouse"] = spouse.toJson();
            }
            
            callback(createApiResponse(200, "获取成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "查询成员详情错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败"));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "获取成员详情异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void MemberController::updateMember(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  int member_id) {
    try {
        // 1. 验证token
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
        
        // 2. 验证对成员的权限
        if (!checkMemberAccess(member_id, user_id)) {
            callback(createApiResponse(403, "无权修改此成员"));
            return;
        }
        
        // 3. 解析请求体
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }
        
        // 4. 检查是否有更新字段
        bool hasUpdateFields = false;
        auto client = app().getDbClient();
        
        try {
            // 先获取当前成员的详细信息
            auto currentResult = client->execSqlSync(
                "SELECT genealogy_id, gender, spouse_id FROM Member WHERE member_id = ?",
                member_id
            );
            
            if (currentResult.empty()) {
                callback(createApiResponse(404, "成员不存在"));
                return;
            }
            
            int genealogy_id = currentResult[0]["genealogy_id"].as<int>();
            std::string current_gender = currentResult[0]["gender"].as<std::string>();
            int current_spouse_id = dbutils::safeInt(currentResult[0], "spouse_id");
            
            // 构建更新语句
            std::string sql = "UPDATE Member SET ";
            std::vector<std::string> params;
            
            // 跟踪是否更新了性别（因为性别会影响婚姻）
            bool gender_changed = false;
            std::string new_gender = current_gender;
            
            if (json->isMember("name")) {
                std::string name = (*json)["name"].asString();
                if (name.empty()) {
                    callback(createApiResponse(400, "姓名不能为空"));
                    return;
                }
                if (hasUpdateFields) sql += ", ";
                sql += "name = ?";
                params.push_back(name);
                hasUpdateFields = true;
            }
            
            if (json->isMember("gender")) {
                new_gender = (*json)["gender"].asString();
                if (!isValidGender(new_gender)) {
                    callback(createApiResponse(400, "性别必须是 male 或 female"));
                    return;
                }
                
                // 如果性别改变，检查是否有配偶
                if (new_gender != current_gender && current_spouse_id > 0) {
                    callback(createApiResponse(400, "已有配偶的成员不能更改性别"));
                    return;
                }
                
                gender_changed = true;
                if (hasUpdateFields) sql += ", ";
                sql += "gender = ?";
                params.push_back(new_gender);
                hasUpdateFields = true;
            }
            
            if (json->isMember("birth_date")) {
                std::string birth_date = (*json)["birth_date"].asString();
                if (!birth_date.empty() && !isValidDate(birth_date)) {
                    callback(createApiResponse(400, "出生日期格式无效 (YYYY-MM-DD)"));
                    return;
                }
                if (hasUpdateFields) sql += ", ";
                sql += "birth_date = NULLIF(?, '')";
                params.push_back(birth_date);
                hasUpdateFields = true;
            }
            
            if (json->isMember("death_date")) {
                std::string death_date = (*json)["death_date"].asString();
                if (!death_date.empty() && !isValidDate(death_date)) {
                    callback(createApiResponse(400, "逝世日期格式无效 (YYYY-MM-DD)"));
                    return;
                }
                if (hasUpdateFields) sql += ", ";
                sql += "death_date = NULLIF(?, '')";
                params.push_back(death_date);
                hasUpdateFields = true;
            }
            
            if (json->isMember("biography")) {
                std::string biography = (*json)["biography"].asString();
                if (hasUpdateFields) sql += ", ";
                sql += "biography = NULLIF(?, '')";
                params.push_back(biography);
                hasUpdateFields = true;
            }
            
            if (json->isMember("father_id")) {
                int father_id = (*json)["father_id"].asInt();
                if (father_id > 0) {
                    // 验证父亲ID
                    auto fatherResult = client->execSqlSync(
                        "SELECT member_id FROM Member WHERE member_id = ? AND genealogy_id = ? AND gender = 'male'",
                        father_id, genealogy_id
                    );
                    if (fatherResult.empty()) {
                        callback(createApiResponse(400, "父亲ID无效或不属于同一族谱"));
                        return;
                    }
                }
                if (hasUpdateFields) sql += ", ";
                sql += "father_id = NULLIF(?, 0)";
                params.push_back(std::to_string(father_id));
                hasUpdateFields = true;
            }
            
            if (json->isMember("mother_id")) {
                int mother_id = (*json)["mother_id"].asInt();
                if (mother_id > 0) {
                    // 验证母亲ID
                    auto motherResult = client->execSqlSync(
                        "SELECT member_id FROM Member WHERE member_id = ? AND genealogy_id = ? AND gender = 'female'",
                        mother_id, genealogy_id
                    );
                    if (motherResult.empty()) {
                        callback(createApiResponse(400, "母亲ID无效或不属于同一族谱"));
                        return;
                    }
                }
                if (hasUpdateFields) sql += ", ";
                sql += "mother_id = NULLIF(?, 0)";
                params.push_back(std::to_string(mother_id));
                hasUpdateFields = true;
            }
            
            // 处理配偶更新（新增逻辑）
            int new_spouse_id = 0;
            if (json->isMember("spouse_id")) {
                new_spouse_id = (*json)["spouse_id"].asInt();
                
                // 验证配偶ID
                if (new_spouse_id > 0) {
                    // 验证配偶存在且属于同一族谱
                    auto spouseResult = client->execSqlSync(
                        "SELECT member_id, gender, spouse_id FROM Member WHERE member_id = ? AND genealogy_id = ?",
                        new_spouse_id, genealogy_id
                    );
                    
                    if (spouseResult.empty()) {
                        callback(createApiResponse(400, "配偶不存在或不属于同一族谱"));
                        return;
                    }
                    
                    // 验证性别：必须为异性
                    std::string spouse_gender = spouseResult[0]["gender"].as<std::string>();
                    if (new_gender == spouse_gender) {
                        callback(createApiResponse(400, "配偶必须是异性"));
                        return;
                    }
                    
                    // 验证配偶是否已有配偶（排除自己）
                    int spouse_current_spouse = spouseResult[0]["spouse_id"].as<int>();
                    if (spouse_current_spouse > 0 && spouse_current_spouse != member_id) {
                        callback(createApiResponse(400, "该成员已有配偶"));
                        return;
                    }
                    
                    // 检查是否试图与自己结婚
                    if (new_spouse_id == member_id) {
                        callback(createApiResponse(400, "不能与自己结婚"));
                        return;
                    }
                }
                
                if (hasUpdateFields) sql += ", ";
                sql += "spouse_id = NULLIF(?, 0)";
                params.push_back(std::to_string(new_spouse_id));
                hasUpdateFields = true;
            }
            
            if (!hasUpdateFields) {
                callback(createApiResponse(400, "没有提供更新字段"));
                return;
            }
            
            sql += " WHERE member_id = ?";
            params.push_back(std::to_string(member_id));
            
            // 开始事务
            client->execSqlSync("START TRANSACTION");
            
            // 处理婚姻关系变更
            if (json->isMember("spouse_id")) {
                // 如果原配偶存在，解除原婚姻关系
                if (current_spouse_id > 0 && current_spouse_id != new_spouse_id) {
                    // 更新原配偶的spouse_id为NULL
                    client->execSqlSync(
                        "UPDATE Member SET spouse_id = NULL WHERE member_id = ?",
                        current_spouse_id
                    );
                    
                    // 将原婚姻记录标记为dissolved
                    client->execSqlSync(
                        "UPDATE Marriage SET status = 'dissolved' WHERE status = 'active' AND "
                        "((husband_id = ? AND wife_id = ?) OR (husband_id = ? AND wife_id = ?))",
                        member_id, current_spouse_id, current_spouse_id, member_id
                    );
                }
                
                // 如果新配偶存在，建立新婚姻关系
                if (new_spouse_id > 0) {
                    // 更新新配偶的spouse_id
                    client->execSqlSync(
                        "UPDATE Member SET spouse_id = ? WHERE member_id = ?",
                        member_id, new_spouse_id
                    );
                    
                    // 在Marriage表中记录新婚姻
                    int husband_id = (new_gender == "male") ? member_id : new_spouse_id;
                    int wife_id = (new_gender == "female") ? member_id : new_spouse_id;
                    
                    client->execSqlSync(
                        "INSERT INTO Marriage (husband_id, wife_id, status) VALUES (?, ?, 'active')",
                        husband_id, wife_id
                    );
                }
            }
            
            // 执行主更新
            if (params.size() == 1) {
                client->execSqlSync(sql, params[0]);
            } else if (params.size() == 2) {
                client->execSqlSync(sql, params[0], params[1]);
            } else if (params.size() == 3) {
                client->execSqlSync(sql, params[0], params[1], params[2]);
            } else if (params.size() == 4) {
                client->execSqlSync(sql, params[0], params[1], params[2], params[3]);
            } else if (params.size() == 5) {
                client->execSqlSync(sql, params[0], params[1], params[2], params[3], params[4]);
            } else if (params.size() == 6) {
                client->execSqlSync(sql, params[0], params[1], params[2], params[3], params[4], params[5]);
            } else if (params.size() == 7) {
                client->execSqlSync(sql, params[0], params[1], params[2], params[3], params[4], params[5], params[6]);
            } else {
                client->execSqlSync("ROLLBACK");
                callback(createApiResponse(500, "参数数量错误"));
                return;
            }
            
            // 提交事务前：当父母变化时，重新计算当前成员 + 所有后代的 generation 和 ancestor_path
            if (json->isMember("father_id") || json->isMember("mother_id")) {
                int nf = json->isMember("father_id") ? (*json)["father_id"].asInt() : 0;
                int nm = json->isMember("mother_id") ? (*json)["mother_id"].asInt() : 0;
                // ★ 修复：使用 safeInt 安全处理 NULL 列（原代码 .as<int>() 会在 NULL 时抛异常）
                if (!json->isMember("father_id")) {
                    auto c = client->execSqlSync("SELECT father_id FROM Member WHERE member_id = ?", member_id);
                    if (!c.empty()) nf = dbutils::safeInt(c[0], "father_id");
                }
                if (!json->isMember("mother_id")) {
                    auto c = client->execSqlSync("SELECT mother_id FROM Member WHERE member_id = ?", member_id);
                    if (!c.empty()) nm = dbutils::safeInt(c[0], "mother_id");
                }
                // 重新计算当前成员辈分（★ 修复：NULL 按 1 处理，始终计算）
                int ng = 1;
                if (nf > 0 || nm > 0) {
                    int pg = 1;  // ★ 修复：默认从 1 开始，确保 NULL generation 不导致辈分扁平
                    if (nf > 0) {
                        auto f = client->execSqlSync("SELECT generation FROM Member WHERE member_id = ?", nf);
                        if (!f.empty() && !f[0]["generation"].isNull()) {
                            int g = f[0]["generation"].as<int>();
                            if (g > pg) pg = g;
                        }
                    }
                    if (nm > 0) {
                        auto f = client->execSqlSync("SELECT generation FROM Member WHERE member_id = ?", nm);
                        if (!f.empty() && !f[0]["generation"].isNull()) {
                            int g = f[0]["generation"].as<int>();
                            if (g > pg) pg = g;
                        }
                    }
                    ng = pg + 1;  // ★ 修复：始终计算，不依赖 pg > 0 守卫
                }
                client->execSqlSync("UPDATE Member SET generation = ? WHERE member_id = ?", ng, member_id);

                // ★ 新增：级联更新所有后代的 generation（原代码缺失此步，是辈分全错的主因！）
                // 用递归 CTE：从当前成员出发，逐层向下修正
                try {
                    client->execSqlSync(R"(
                        WITH RECURSIVE cascade_gen AS (
                            SELECT member_id, ? AS new_gen
                            FROM Member WHERE member_id = ?

                            UNION ALL

                            SELECT m.member_id, c.new_gen + 1
                            FROM Member m
                            JOIN cascade_gen c ON m.father_id = c.member_id OR m.mother_id = c.member_id
                        )
                        UPDATE Member m
                        JOIN cascade_gen c ON m.member_id = c.member_id
                        SET m.generation = c.new_gen
                        WHERE m.member_id != ?
                    )", ng, member_id, member_id);
                } catch (const std::exception& e) {
                    std::cerr << "级联更新后代 generation 警告: " << e.what() << std::endl;
                }

                // Phase 3: Materialized Path - 重新计算 ancestor_path
                try {
                    std::string ancestor_path;
                    int parent_id = (nf > 0) ? nf : nm;
                    if (parent_id > 0) {
                        auto parentPathResult = client->execSqlSync(
                            "SELECT ancestor_path FROM Member WHERE member_id = ?", parent_id
                        );
                        if (!parentPathResult.empty() && !parentPathResult[0]["ancestor_path"].isNull()) {
                            std::string parent_path = parentPathResult[0]["ancestor_path"].as<std::string>();
                            ancestor_path = parent_path + "/" + std::to_string(member_id);
                        } else {
                            ancestor_path = "/" + std::to_string(parent_id) + "/" + std::to_string(member_id);
                        }
                    } else {
                        ancestor_path = "/" + std::to_string(member_id);
                    }
                    client->execSqlSync(
                        "UPDATE Member SET ancestor_path = ? WHERE member_id = ?",
                        ancestor_path, member_id
                    );

                    // ★ 修复：级联更新所有后代的 ancestor_path
                    // 原代码用 SUBSTRING_INDEX + REPLACE，当 member_id 数字是另一个路径段子串时会错误匹配
                    // （如 member_id=4 在路径 "/1/14/4/..." 中会被误匹配到 "/14" 里的 "4"）
                    // 改用递归 CTE 从当前成员出发，精确重建所有后代路径
                    client->execSqlSync(R"(
                        WITH RECURSIVE subtree AS (
                            SELECT member_id, ? AS new_path
                            FROM Member WHERE member_id = ?

                            UNION ALL

                            SELECT m.member_id, CONCAT(s.new_path, '/', m.member_id)
                            FROM Member m
                            JOIN subtree s ON m.father_id = s.member_id OR m.mother_id = s.member_id
                        )
                        UPDATE Member m
                        JOIN subtree s ON m.member_id = s.member_id
                        SET m.ancestor_path = s.new_path
                        WHERE m.member_id != ?
                    )", ancestor_path, member_id, member_id);
                } catch (const std::exception& e) {
                    std::cerr << "更新 ancestor_path 警告: " << e.what() << std::endl;
                }
            }

            client->execSqlSync("COMMIT");

            // Phase 3: 数据变更时失效统计缓存
            cache::StatsCache::instance().invalidatePrefix("dashboard_" + std::to_string(genealogy_id));
            cache::StatsCache::instance().invalidatePrefix("stats_" + std::to_string(genealogy_id));

            // 返回成功响应
            callback(createApiResponse(200, "更新成功"));
            
        } catch (const std::exception& e) {
            // 回滚事务
            try { client->execSqlSync("ROLLBACK"); } catch (...) {}
            
            std::cerr << "更新成员错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "更新失败: " + std::string(e.what())));
        }
        
    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "更新成员异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void MemberController::deleteMember(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback,
                                  int member_id) {
    try {
        // 1. 验证token
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
        
        // 2. 验证对成员的权限
        if (!checkMemberAccess(member_id, user_id)) {
            callback(createApiResponse(403, "无权删除此成员"));
            return;
        }
        
        // 3. 检查是否可以删除
        if (!canDeleteMember(member_id)) {
            callback(createApiResponse(400, "无法删除成员，该成员已有后代"));
            return;
        }
        
        // 4. 删除成员（处理婚姻关系）
        auto client = app().getDbClient();
        try {
            // Phase 3: 先获取 genealogy_id 用于缓存失效
            int genealogy_id = 0;
            auto gidResult = client->execSqlSync(
                "SELECT genealogy_id FROM Member WHERE member_id = ?", member_id
            );
            if (!gidResult.empty()) genealogy_id = gidResult[0]["genealogy_id"].as<int>();

            // 开始事务
            client->execSqlSync("START TRANSACTION");
            
            // 首先获取配偶ID
            auto spouseResult = client->execSqlSync(
                "SELECT spouse_id FROM Member WHERE member_id = ?",
                member_id
            );
            
            int spouse_id = 0;
            if (!spouseResult.empty() && !spouseResult[0]["spouse_id"].isNull()) {
                spouse_id = spouseResult[0]["spouse_id"].as<int>();
            }
            
            // 如果有配偶，解除婚姻关系
            if (spouse_id > 0) {
                // 更新配偶的spouse_id为NULL
                client->execSqlSync(
                    "UPDATE Member SET spouse_id = NULL WHERE member_id = ?",
                    spouse_id
                );
                
                // 在Marriage表中标记为dissolved（如果记录存在）
                // 注意：我们简化设计，不记录历史，所以这里可以选择删除记录
                client->execSqlSync(
                    "DELETE FROM Marriage WHERE (husband_id = ? AND wife_id = ?) OR (husband_id = ? AND wife_id = ?)",
                    member_id, spouse_id, spouse_id, member_id
                );
            }
            
            // 删除成员
            client->execSqlSync("DELETE FROM Member WHERE member_id = ?", member_id);
            
            // 提交事务
            client->execSqlSync("COMMIT");

            // Phase 3: 数据变更时失效统计缓存
            if (genealogy_id > 0) {
                cache::StatsCache::instance().invalidatePrefix("dashboard_" + std::to_string(genealogy_id));
                cache::StatsCache::instance().invalidatePrefix("stats_" + std::to_string(genealogy_id));
            }

            callback(createApiResponse(200, "删除成功"));
            
        } catch (const std::exception& e) {
            // 回滚事务
            try { client->execSqlSync("ROLLBACK"); } catch (...) {}
            
            std::cerr << "删除成员错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "删除失败: " + std::string(e.what())));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "删除成员异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void MemberController::searchMember(const HttpRequestPtr& req,
                                  std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        // 1. 验证token
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
        
        // 2. 获取搜索参数
        std::string keyword = req->getParameter("keyword");
        if (keyword.empty()) {
            callback(createApiResponse(400, "搜索关键词不能为空"));
            return;
        }
        
        int page = 1;
        int page_size = 20;
        
        auto page_str = req->getParameter("page");
        if (!page_str.empty()) {
            try {
                page = std::stoi(page_str);
                if (page < 1) page = 1;
            } catch (...) {
                callback(createApiResponse(400, "页码参数无效"));
                return;
            }
        }
        
        auto page_size_str = req->getParameter("page_size");
        if (!page_size_str.empty()) {
            try {
                page_size = std::stoi(page_size_str);
                if (page_size < 1) page_size = 20;
                if (page_size > 500) page_size = 500;  // 限制最大500条
            } catch (...) {
                callback(createApiResponse(400, "每页数量参数无效"));
                return;
            }
        }
        
        int offset = (page - 1) * page_size;
        
        // 3. 搜索数据库（Phase 3: 优先使用全文搜索，回退到 LIKE）
        auto client = app().getDbClient();
        try {
            // Phase 3: 全文搜索优化（替代 LIKE '%keyword%'）
            // MATCH AGAINST 比 LIKE 快 10-100 倍（对 10 万条记录）
            // 关键词太短（<3字符）时回退到 LIKE（MySQL 默认 ft_min_word_len=4）
            bool use_fulltext = (keyword.length() >= 3);

            // 查询总数（优先 FULLTEXT，失败或 0 结果时回退 LIKE）
            int total = 0;
            if (use_fulltext) {
                try {
                    auto countResult = client->execSqlSync(R"(
                        SELECT COUNT(*) as total FROM Member m
                        JOIN Genealogy g ON m.genealogy_id = g.genealogy_id
                        WHERE g.user_id = ? AND MATCH(m.name) AGAINST(? IN BOOLEAN MODE)
                    )", user_id, keyword);
                    if (!countResult.empty()) {
                        total = countResult[0]["total"].as<int>();
                    }
                    // FULLTEXT 返回 0 行可能是中文分词问题，回退到 LIKE
                    if (total == 0) {
                        use_fulltext = false;
                    }
                } catch (const std::exception& e) {
                    // FULLTEXT 执行失败（如索引损坏），回退到 LIKE
                    use_fulltext = false;
                }
            }
            if (!use_fulltext) {
                std::string search_pattern = "%" + keyword + "%";
                auto countResult = client->execSqlSync(
                    "SELECT COUNT(*) as total FROM Member m "
                    "JOIN Genealogy g ON m.genealogy_id = g.genealogy_id "
                    "WHERE g.user_id = ? AND m.name LIKE ?",
                    user_id, search_pattern
                );
                if (!countResult.empty()) {
                    total = countResult[0]["total"].as<int>();
                }
            }

            // 查询搜索结果
            drogon::orm::Result result = (use_fulltext)
                ? client->execSqlSync(R"(
                    SELECT m.*, g.name as genealogy_name,
                           s.member_id as spouse_member_id, s.name as spouse_name,
                           s.gender as spouse_gender, s.birth_date as spouse_birth_date,
                           s.death_date as spouse_death_date, s.generation as spouse_generation,
                           s.genealogy_id as spouse_genealogy_id
                    FROM Member m
                    JOIN Genealogy g ON m.genealogy_id = g.genealogy_id
                    LEFT JOIN Member s ON m.spouse_id = s.member_id
                    WHERE g.user_id = ? AND MATCH(m.name) AGAINST(? IN BOOLEAN MODE)
                    ORDER BY m.member_id DESC LIMIT ? OFFSET ?
                )", user_id, keyword, page_size, offset)
                : [&]() {
                    std::string search_pattern = "%" + keyword + "%";
                    return client->execSqlSync(R"(
                        SELECT m.*, g.name as genealogy_name,
                               s.member_id as spouse_member_id, s.name as spouse_name,
                               s.gender as spouse_gender, s.birth_date as spouse_birth_date,
                               s.death_date as spouse_death_date, s.generation as spouse_generation,
                               s.genealogy_id as spouse_genealogy_id
                        FROM Member m
                        JOIN Genealogy g ON m.genealogy_id = g.genealogy_id
                        LEFT JOIN Member s ON m.spouse_id = s.member_id
                        WHERE g.user_id = ? AND m.name LIKE ?
                        ORDER BY m.member_id DESC LIMIT ? OFFSET ?
                    )", user_id, search_pattern, page_size, offset);
                }();
            
            Value data;
            Value results(Json::arrayValue);
            
            for (const auto& row : result) {
                models::Member member;
                member.member_id = row["member_id"].as<int>();
                member.genealogy_id = row["genealogy_id"].as<int>();
                member.name = row["name"].as<std::string>();
                member.gender = row["gender"].as<std::string>();
                member.birth_date = dbutils::safeStr(row, "birth_date");
                member.death_date = dbutils::safeStr(row, "death_date");
                member.biography = dbutils::safeStr(row, "biography");
                member.father_id = dbutils::safeInt(row, "father_id");
                member.mother_id = dbutils::safeInt(row, "mother_id");
                member.spouse_id = dbutils::safeInt(row, "spouse_id");
                member.generation = dbutils::safeInt(row, "generation");

                // 添加额外的族谱名称信息
                Value memberJson = member.toJson();
                memberJson["genealogy_name"] = dbutils::safeStr(row, "genealogy_name");

                // 使用 LEFT JOIN 结果添加配偶详情（消除 N+1）
                if (!row["spouse_member_id"].isNull()) {
                    models::Member spouse;
                    spouse.member_id = dbutils::safeInt(row, "spouse_member_id");
                    spouse.genealogy_id = dbutils::safeInt(row, "spouse_genealogy_id");
                    spouse.name = dbutils::safeStr(row, "spouse_name");
                    spouse.gender = dbutils::safeStr(row, "spouse_gender");
                    spouse.birth_date = dbutils::safeStr(row, "spouse_birth_date");
                    spouse.death_date = dbutils::safeStr(row, "spouse_death_date");
                    spouse.generation = dbutils::safeInt(row, "spouse_generation");

                    memberJson["spouse"] = spouse.toJson();
                }

                results.append(memberJson);
            }
            
            data["results"] = results;
            data["total"] = total;
            data["keyword"] = keyword;
            data["page"] = page;
            data["page_size"] = page_size;
            data["total_pages"] = (total + page_size - 1) / page_size;
            
            callback(createApiResponse(200, "搜索成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "搜索成员错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "搜索失败"));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "搜索成员异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// ============================================================
// 家族树专用接口（前端问题 #1 修复）
// GET /api/genealogy/{gid}/tree
// 返回精简字段、无分页上限，专为族谱树渲染设计
// ============================================================
void MemberController::getTreeData(const drogon::HttpRequestPtr& req,
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

        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权查看此族谱"));
            return;
        }

        auto client = app().getDbClient();
        try {
            // 精简字段，无 LIMIT，专为前端树渲染设计
            auto result = client->execSqlSync(R"(
                SELECT member_id, name, gender, generation,
                       father_id, mother_id, spouse_id,
                       birth_date, death_date
                FROM Member
                WHERE genealogy_id = ?
                ORDER BY generation, birth_date
            )", genealogy_id);

            Value members(Json::arrayValue);
            for (const auto& row : result) {
                Value m;
                m["member_id"]   = row["member_id"].as<int>();
                m["name"]        = row["name"].as<std::string>();
                m["gender"]      = row["gender"].as<std::string>();
                m["generation"]  = dbutils::safeInt(row, "generation");
                m["father_id"]   = dbutils::safeInt(row, "father_id");
                m["mother_id"]   = dbutils::safeInt(row, "mother_id");
                m["spouse_id"]   = dbutils::safeInt(row, "spouse_id");
                m["birth_date"]  = dbutils::safeStr(row, "birth_date");
                m["death_date"]  = dbutils::safeStr(row, "death_date");
                members.append(m);
            }

            Value data;
            data["genealogy_id"] = genealogy_id;
            data["members"]      = members;
            data["total"]        = static_cast<int>(members.size());

            auto resp = createApiResponse(200, "获取成功", data);
            resp->addHeader("Cache-Control", "public, max-age=60");
            callback(resp);

        } catch (const std::exception& e) {
            std::cerr << "查询家族树数据错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "查询家族树数据异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// ============================================================
// 批量获取成员详情
// POST /api/member/batch
// Body: { "member_ids": [1, 2, 3, ...] }
// 一次性返回所有请求成员的信息（消除 N+1 批量查询）
// ============================================================
void MemberController::getMembersBatch(const drogon::HttpRequestPtr& req,
                                      std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        // 1. 验证token
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

        // 2. 解析请求体
        auto json = req->getJsonObject();
        if (!json || !json->isMember("member_ids")) {
            callback(createApiResponse(400, "请求体必须包含 member_ids 数组"));
            return;
        }

        const auto& ids = (*json)["member_ids"];
        if (!ids.isArray() || ids.empty()) {
            callback(createApiResponse(400, "member_ids 必须是非空数组"));
            return;
        }

        std::vector<int> member_ids;
        for (const auto& id : ids) {
            member_ids.push_back(id.asInt());
        }

        if (member_ids.size() > 1000) {
            callback(createApiResponse(400, "一次最多查询 1000 个成员"));
            return;
        }

        // 3. 构建带占位符的 SQL（直接嵌入整数 ID，无 SQL 注入风险）
        std::string id_list;
        for (size_t i = 0; i < member_ids.size(); ++i) {
            if (i > 0) id_list += ",";
            id_list += std::to_string(member_ids[i]);
        }

        std::string sql = R"(
            SELECT member_id, name, gender, generation,
                   father_id, mother_id, spouse_id,
                   birth_date, death_date, genealogy_id
            FROM Member
            WHERE member_id IN ()" + id_list + R"()
            ORDER BY FIELD(member_id,)" + id_list + R"()
        )";

        // 执行查询
        auto client = app().getDbClient();
        try {
            auto result = client->execSqlSync(sql);

            std::unordered_map<int, Value> member_map;
            for (const auto& row : result) {
                Value m;
                m["member_id"]    = row["member_id"].as<int>();
                m["name"]         = row["name"].as<std::string>();
                m["gender"]       = row["gender"].as<std::string>();
                m["generation"]   = dbutils::safeInt(row, "generation");
                m["father_id"]    = dbutils::safeInt(row, "father_id");
                m["mother_id"]    = dbutils::safeInt(row, "mother_id");
                m["spouse_id"]    = dbutils::safeInt(row, "spouse_id");
                m["birth_date"]   = dbutils::safeStr(row, "birth_date");
                m["death_date"]   = dbutils::safeStr(row, "death_date");
                m["genealogy_id"] = row["genealogy_id"].as<int>();
                member_map[row["member_id"].as<int>()] = m;
            }

            Value members(Json::arrayValue);
            for (int mid : member_ids) {
                auto it = member_map.find(mid);
                if (it != member_map.end()) {
                    members.append(it->second);
                }
            }

            Value data;
            data["members"] = members;
            data["total"]   = static_cast<int>(members.size());

            auto resp = createApiResponse(200, "获取成功", data);
            resp->addHeader("Cache-Control", "public, max-age=60");
            callback(resp);

        } catch (const std::exception& e) {
            std::cerr << "批量查询成员错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "批量查询成员异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}