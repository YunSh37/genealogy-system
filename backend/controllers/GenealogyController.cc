// controllers/GenealogyController.cc
#include "GenealogyController.h"
#include "AuthController.h"
#include "ResponseUtils.h"
#include "models/Genealogy.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>
#include <vector>

using namespace api::v1;
using namespace drogon;
using Json::Value;
using myutils::createApiResponse;

namespace {
    // 辅助函数：将参数打包为元组
    template<typename... Args>
    void execDynamicSqlImpl(const drogon::orm::DbClientPtr& client, 
                          const std::string& sql, 
                          const std::tuple<Args...>& args) {
        // 使用std::apply展开元组参数
        std::apply([&client, &sql](const auto&... params) {
            client->execSqlSync(sql, params...);
        }, args);
    }
    
    // 主辅助函数：支持动态数量的参数
    template<typename... Args>
    void execDynamicSql(const drogon::orm::DbClientPtr& client, 
                       const std::string& sql, 
                       Args&&... args) {
        // 将所有参数打包为元组
        auto tupleArgs = std::make_tuple(std::forward<Args>(args)...);
        execDynamicSqlImpl(client, sql, tupleArgs);
    }
}

// 从请求头获取token
std::string getTokenFromHeader(const HttpRequestPtr& req) {
    // 尝试从Authorization头获取
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0) {
        return authHeader.substr(7);
    }
    
    // 尝试从自定义头X-Token获取
    auto tokenHeader = req->getHeader("X-Token");
    if (!tokenHeader.empty()) {
        return tokenHeader;
    }
    
    return "";
}

int GenealogyController::getUserIdFromToken(const std::string& token) {
    if (token.empty()) {
        std::cout << "Token 为空" << std::endl;
        return 0;
    }
    
    // 使用AuthController的静态方法验证token
    auto [userId, username] = AuthController::validateToken(token);
    if (userId == 0) {
        std::cout << "Token 验证失败: " << token << std::endl;
    }
    return userId;
}

// 验证族谱所有权
bool GenealogyController::checkGenealogyOwnership(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ?",
            genealogy_id
        );
        
        if (!result.empty()) {
            int owner_id = result[0]["user_id"].as<int>();
            return owner_id == user_id;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "检查族谱所有权错误: " << e.what() << std::endl;
        return false;
    }
}

// 验证族谱名称是否重复
bool GenealogyController::isGenealogyNameExists(const std::string& name, int user_id, int exclude_id) {
    auto client = app().getDbClient();
    try {
        if (exclude_id > 0) {
            auto result = client->execSqlSync(
                "SELECT genealogy_id FROM Genealogy WHERE name = ? AND user_id = ? AND genealogy_id != ?",
                name, user_id, exclude_id
            );
            return !result.empty();
        } else {
            auto result = client->execSqlSync(
                "SELECT genealogy_id FROM Genealogy WHERE name = ? AND user_id = ?",
                name, user_id
            );
            return !result.empty();
        }
    } catch (const std::exception& e) {
        std::cerr << "检查族谱名称重复错误: " << e.what() << std::endl;
        return false;
    }
}

// 验证必填字段
bool GenealogyController::validateRequiredFields(const Json::Value& json, const std::vector<std::string>& fields) {
    for (const auto& field : fields) {
        if (!json.isMember(field) || json[field].asString().empty()) {
            return false;
        }
    }
    return true;
}


void GenealogyController::createGenealogy(const HttpRequestPtr& req,
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
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }
        
        // 3. 验证必填字段
        std::vector<std::string> requiredFields = {"name", "surname"};
        if (!validateRequiredFields(*json, requiredFields)) {
            callback(createApiResponse(400, "族谱名称和姓氏为必填项"));
            return;
        }
        
        std::string name = (*json)["name"].asString();
        std::string surname = (*json)["surname"].asString();
        std::string founder_name = json->isMember("founder_name") ? (*json)["founder_name"].asString() : "";
        std::string description = json->isMember("description") ? (*json)["description"].asString() : "";
        
        // 4. 验证族谱名称是否重复
        if (isGenealogyNameExists(name, user_id)) {
            callback(createApiResponse(400, "族谱名称已存在"));
            return;
        }
        
        // 5. 插入数据库
        auto client = app().getDbClient();
        try {
            client->execSqlSync(
                "INSERT INTO Genealogy (name, surname, founder_name, description, user_id) VALUES (?, ?, ?, ?, ?)",
                name, surname, founder_name, description, user_id
            );
            
            // 获取新创建的族谱ID
            auto result = client->execSqlSync(
                "SELECT genealogy_id, create_time FROM Genealogy WHERE name = ? AND user_id = ? ORDER BY genealogy_id DESC LIMIT 1",
                name, user_id
            );
            
            if (!result.empty()) {
                models::Genealogy genealogy;
                genealogy.genealogy_id = result[0]["genealogy_id"].as<int>();
                genealogy.name = name;
                genealogy.surname = surname;
                genealogy.founder_name = founder_name;
                genealogy.description = description;
                genealogy.user_id = user_id;
                genealogy.create_time = result[0]["create_time"].as<std::string>();
                
                Value data = genealogy.toJson();
                callback(createApiResponse(201, "创建成功", data));
            } else {
                callback(createApiResponse(500, "创建失败，无法获取族谱ID"));
            }
            
        } catch (const std::exception& e) {
            std::cerr << "创建族谱错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "创建失败: " + std::string(e.what())));
        }
        
    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "创建族谱异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void GenealogyController::getGenealogyList(const HttpRequestPtr& req,
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
        
        // 2. 查询数据库：用户创建的族谱 + 被共享的族谱
        auto client = app().getDbClient();
        try {
            // 查询用户创建的族谱
            auto ownedResult = client->execSqlSync(
                "SELECT g.*, 'owner' as role FROM Genealogy g WHERE g.user_id = ? "
                "ORDER BY g.create_time DESC",
                user_id
            );
            
            // 查询被共享的族谱
            auto sharedResult = client->execSqlSync(
                "SELECT g.*, s.permission as role FROM Genealogy g "
                "JOIN GenealogyShare s ON g.genealogy_id = s.genealogy_id "
                "WHERE s.user_id = ? ORDER BY s.created_at DESC",
                user_id
            );
            
            Value data;
            Value genealogies(Json::arrayValue);
            
            // 添加用户创建的族谱
            for (const auto& row : ownedResult) {
                models::Genealogy genealogy;
                genealogy.genealogy_id = row["genealogy_id"].as<int>();
                genealogy.name = row["name"].as<std::string>();
                genealogy.surname = row["surname"].as<std::string>();
                genealogy.founder_name = row["founder_name"].as<std::string>();
                genealogy.description = row["description"].as<std::string>();
                genealogy.create_time = row["create_time"].as<std::string>();
                genealogy.user_id = row["user_id"].as<int>();
                genealogy.role = row["role"].as<std::string>();
                
                genealogies.append(genealogy.toJson());
            }
            
            // 添加被共享的族谱
            for (const auto& row : sharedResult) {
                models::Genealogy genealogy;
                genealogy.genealogy_id = row["genealogy_id"].as<int>();
                genealogy.name = row["name"].as<std::string>();
                genealogy.surname = row["surname"].as<std::string>();
                genealogy.founder_name = row["founder_name"].as<std::string>();
                genealogy.description = row["description"].as<std::string>();
                genealogy.create_time = row["create_time"].as<std::string>();
                genealogy.user_id = row["user_id"].as<int>();
                genealogy.role = row["role"].as<std::string>();
                
                genealogies.append(genealogy.toJson());
            }
            
            data["genealogies"] = genealogies;
            data["total"] = static_cast<int>(ownedResult.size() + sharedResult.size());
            
            callback(createApiResponse(200, "获取成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "查询族谱列表错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败"));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "获取族谱列表异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void GenealogyController::getGenealogyDetail(const HttpRequestPtr& req,
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
        
        // 2. 查询数据库
        auto client = app().getDbClient();
        try {
            auto result = client->execSqlSync(
                "SELECT * FROM Genealogy WHERE genealogy_id = ?",
                genealogy_id
            );
            
            if (result.empty()) {
                callback(createApiResponse(404, "族谱不存在"));
                return;
            }
            
            // 3. 检查权限（创建者或共享用户均可查看）
            int owner_id = result[0]["user_id"].as<int>();
            std::string user_role = "none";
            if (owner_id == user_id) {
                user_role = "owner";
            } else {
                // 检查是否在共享列表中
                auto shareResult = client->execSqlSync(
                    "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
                    genealogy_id, user_id
                );
                if (shareResult.empty()) {
                    callback(createApiResponse(403, "无权查看此族谱"));
                    return;
                }
                user_role = shareResult[0]["permission"].as<std::string>();
            }
            
            // 4. 返回族谱详情
            models::Genealogy genealogy;
            genealogy.genealogy_id = result[0]["genealogy_id"].as<int>();
            genealogy.name = result[0]["name"].as<std::string>();
            genealogy.surname = result[0]["surname"].as<std::string>();
            genealogy.founder_name = result[0]["founder_name"].as<std::string>();
            genealogy.description = result[0]["description"].as<std::string>();
            genealogy.create_time = result[0]["create_time"].as<std::string>();
            genealogy.user_id = result[0]["user_id"].as<int>();
            genealogy.role = user_role;
            
            Value data = genealogy.toJson();
            callback(createApiResponse(200, "获取成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "查询族谱详情错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败"));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "获取族谱详情异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void GenealogyController::updateGenealogy(const HttpRequestPtr& req,
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
        
        // 2. 验证族谱所有权
        if (!checkGenealogyOwnership(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权修改此族谱"));
            return;
        }
        
        // 3. 解析请求体
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }
        
        // 4. 构建更新语句
        auto client = app().getDbClient();
        try {
            // 检查是否有更新字段
            bool hasUpdateFields = false;
            std::vector<std::pair<std::string, std::string>> updates;
            
            if (json->isMember("name")) {
                std::string name = (*json)["name"].asString();
                if (name.empty()) {
                    callback(createApiResponse(400, "族谱名称不能为空"));
                    return;
                }
                // 检查名称是否重复（排除当前族谱）
                if (isGenealogyNameExists(name, user_id, genealogy_id)) {
                    callback(createApiResponse(400, "族谱名称已存在"));
                    return;
                }
                updates.emplace_back("name", name);
                hasUpdateFields = true;
            }
            
            if (json->isMember("surname")) {
                std::string surname = (*json)["surname"].asString();
                if (surname.empty()) {
                    callback(createApiResponse(400, "姓氏不能为空"));
                    return;
                }
                updates.emplace_back("surname", surname);
                hasUpdateFields = true;
            }
            
            if (json->isMember("founder_name")) {
                updates.emplace_back("founder_name", (*json)["founder_name"].asString());
                hasUpdateFields = true;
            }
            
            if (json->isMember("description")) {
                updates.emplace_back("description", (*json)["description"].asString());
                hasUpdateFields = true;
            }
            
            if (!hasUpdateFields) {
                callback(createApiResponse(400, "没有提供更新字段"));
                return;
            }
            
            // 构建SQL更新语句
            std::string sql = "UPDATE Genealogy SET ";
            
            for (size_t i = 0; i < updates.size(); i++) {
                if (i > 0) sql += ", ";
                sql += updates[i].first + " = ?";
            }
            
            sql += " WHERE genealogy_id = ?";
            
            // 使用可变参数模板辅助函数执行SQL
            // 构建参数列表
            if (updates.size() == 1) {
                execDynamicSql(client, sql, 
                    updates[0].second,  // 第1个参数
                    std::to_string(genealogy_id)  // genealogy_id
                );
            } else if (updates.size() == 2) {
                execDynamicSql(client, sql, 
                    updates[0].second,  // 第1个参数
                    updates[1].second,  // 第2个参数
                    std::to_string(genealogy_id)  // genealogy_id
                );
            } else if (updates.size() == 3) {
                execDynamicSql(client, sql, 
                    updates[0].second,  // 第1个参数
                    updates[1].second,  // 第2个参数
                    updates[2].second,  // 第3个参数
                    std::to_string(genealogy_id)  // genealogy_id
                );
            } else if (updates.size() == 4) {
                execDynamicSql(client, sql, 
                    updates[0].second,  // 第1个参数
                    updates[1].second,  // 第2个参数
                    updates[2].second,  // 第3个参数
                    updates[3].second,  // 第4个参数
                    std::to_string(genealogy_id)  // genealogy_id
                );
            } else {
                callback(createApiResponse(500, "参数数量错误"));
                return;
            }
            
            callback(createApiResponse(200, "更新成功"));
            
        } catch (const std::exception& e) {
            std::cerr << "更新族谱错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "更新失败: " + std::string(e.what())));
        }
        
    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "更新族谱异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void GenealogyController::deleteGenealogy(const HttpRequestPtr& req,
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
        
        // 2. 验证族谱所有权
        if (!checkGenealogyOwnership(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权删除此族谱"));
            return;
        }
        
        // 3. 检查是否有成员依赖
        auto client = app().getDbClient();
        try {
            // 检查是否有成员属于此族谱
            auto membersResult = client->execSqlSync(
                "SELECT COUNT(*) as count FROM Member WHERE genealogy_id = ?",
                genealogy_id
            );
            
            int memberCount = membersResult[0]["count"].as<int>();
            if (memberCount > 0) {
                callback(createApiResponse(400, "无法删除族谱，请先删除所有成员"));
                return;
            }
            
            // 删除族谱
            client->execSqlSync("DELETE FROM Genealogy WHERE genealogy_id = ?", genealogy_id);
            
            callback(createApiResponse(200, "删除成功"));
            
        } catch (const std::exception& e) {
            std::cerr << "删除族谱错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "删除失败: " + std::string(e.what())));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "删除族谱异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// 检查用户是否有编辑权限
bool GenealogyController::hasEditPermission(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        // 首先检查是否是创建者
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!result.empty()) {
            return true;  // 创建者有完全权限
        }
        
        // 然后检查是否在共享列表中且有编辑权限
        auto shareResult = client->execSqlSync(
            "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!shareResult.empty()) {
            std::string permission = shareResult[0]["permission"].as<std::string>();
            return permission == "edit";  // 有编辑权限
        }
        
        return false;
    } catch (const std::exception& e) {
        std::cerr << "检查编辑权限错误: " << e.what() << std::endl;
        return false;
    }
}

// 检查用户是否有查看权限
bool GenealogyController::hasViewPermission(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        // 首先检查是否是创建者
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!result.empty()) {
            return true;  // 创建者有查看权限
        }
        
        // 然后检查是否在共享列表中
        auto shareResult = client->execSqlSync(
            "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        return !shareResult.empty();  // 在共享列表中就有查看权限
    } catch (const std::exception& e) {
        std::cerr << "检查查看权限错误: " << e.what() << std::endl;
        return false;
    }
}

// 获取用户在族谱中的角色
std::string GenealogyController::getUserRole(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        // 检查是否是创建者
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!result.empty()) {
            return "owner";
        }
        
        // 检查共享权限
        auto shareResult = client->execSqlSync(
            "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!shareResult.empty()) {
            std::string permission = shareResult[0]["permission"].as<std::string>();
            return permission;  // 返回 "edit" 或 "view"
        }
        
        return "none";  // 无权限
    } catch (const std::exception& e) {
        std::cerr << "获取用户角色错误: " << e.what() << std::endl;
        return "none";
    }
}