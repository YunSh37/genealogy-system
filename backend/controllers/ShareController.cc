// controllers/ShareController.cc
#include "ShareController.h"
#include "AuthController.h"
#include "ResponseUtils.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>

using namespace api::v1;
using namespace drogon;
using Json::Value;
using myutils::createApiResponse;

std::string ShareController::getTokenFromHeader(const HttpRequestPtr& req) {
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

int ShareController::getUserIdFromToken(const std::string& token) {
    if (token.empty()) {
        return 0;
    }
    
    auto [userId, username] = AuthController::validateToken(token);
    return userId;
}

bool ShareController::isGenealogyOwner(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        return !result.empty();
    } catch (const std::exception& e) {
        std::cerr << "检查族谱所有权错误: " << e.what() << std::endl;
        return false;
    }
}

bool ShareController::userExists(int user_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT user_id FROM User WHERE user_id = ? AND status = 'active'",
            user_id
        );
        
        return !result.empty();
    } catch (const std::exception& e) {
        std::cerr << "检查用户存在错误: " << e.what() << std::endl;
        return false;
    }
}

bool ShareController::isUserShared(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT share_id FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        return !result.empty();
    } catch (const std::exception& e) {
        std::cerr << "检查用户共享状态错误: " << e.what() << std::endl;
        return false;
    }
}

void ShareController::addShare(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback,
                             int genealogy_id) {
    try {
        // 1. 验证token
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }
        
        int current_user_id = getUserIdFromToken(token);
        if (current_user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }
        
        // 2. 验证是否是族谱的创建者
        if (!isGenealogyOwner(genealogy_id, current_user_id)) {
            callback(createApiResponse(403, "只有族谱创建者可以添加共享用户"));
            return;
        }
        
        // 3. 解析请求体
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }
        
        if (!json->isMember("user_id")) {
            callback(createApiResponse(400, "必须提供user_id"));
            return;
        }
        
        int target_user_id = (*json)["user_id"].asInt();
        
        // 4. 验证目标用户
        if (target_user_id == current_user_id) {
            callback(createApiResponse(400, "不能共享给自己"));
            return;
        }
        
        if (!userExists(target_user_id)) {
            callback(createApiResponse(404, "目标用户不存在或已注销"));
            return;
        }
        
        // 5. 检查是否已经在共享列表中
        if (isUserShared(genealogy_id, target_user_id)) {
            callback(createApiResponse(400, "该用户已在共享列表中"));
            return;
        }
        
        // 6. 获取权限（默认为edit）
        std::string permission = "edit";
        if (json->isMember("permission")) {
            permission = (*json)["permission"].asString();
            if (permission != "edit" && permission != "view") {
                callback(createApiResponse(400, "权限只能是 edit 或 view"));
                return;
            }
        }
        
        // 7. 插入共享记录
        auto client = app().getDbClient();
        try {
            client->execSqlSync(
                "INSERT INTO GenealogyShare (genealogy_id, user_id, permission) VALUES (?, ?, ?)",
                genealogy_id, target_user_id, permission
            );
            
            // 获取共享用户信息
            auto userResult = client->execSqlSync(
                "SELECT username FROM User WHERE user_id = ?",
                target_user_id
            );
            
            Value data;
            if (!userResult.empty()) {
                data["user_id"] = target_user_id;
                data["username"] = userResult[0]["username"].as<std::string>();
                data["permission"] = permission;
            }
            
            callback(createApiResponse(201, "添加共享成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "添加共享错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "添加共享失败: " + std::string(e.what())));
        }
        
    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "添加共享异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void ShareController::removeShare(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                int genealogy_id, int user_id) {
    try {
        // 1. 验证token
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }
        
        int current_user_id = getUserIdFromToken(token);
        if (current_user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }
        
        // 2. 验证是否是族谱的创建者
        if (!isGenealogyOwner(genealogy_id, current_user_id)) {
            callback(createApiResponse(403, "只有族谱创建者可以删除共享用户"));
            return;
        }
        
        // 3. 删除共享记录
        auto client = app().getDbClient();
        try {
            client->execSqlSync(
                "DELETE FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
                genealogy_id, user_id
            );
            
            callback(createApiResponse(200, "删除共享成功"));
            
        } catch (const std::exception& e) {
            std::cerr << "删除共享错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "删除共享失败: " + std::string(e.what())));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "删除共享异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void ShareController::getShareList(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback,
                                 int genealogy_id) {
    try {
        // 1. 验证token
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }
        
        int current_user_id = getUserIdFromToken(token);
        if (current_user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }
        
        // 2. 验证是否是族谱的创建者
        if (!isGenealogyOwner(genealogy_id, current_user_id)) {
            callback(createApiResponse(403, "只有族谱创建者可以查看共享列表"));
            return;
        }
        
        // 3. 获取共享用户列表
        auto client = app().getDbClient();
        try {
            auto result = client->execSqlSync(
                "SELECT s.*, u.username FROM GenealogyShare s "
                "JOIN User u ON s.user_id = u.user_id "
                "WHERE s.genealogy_id = ? ORDER BY s.created_at DESC",
                genealogy_id
            );
            
            Value data;
            Value shares(Json::arrayValue);
            
            for (const auto& row : result) {
                Value share;
                share["share_id"] = row["share_id"].as<int>();
                share["user_id"] = row["user_id"].as<int>();
                share["username"] = row["username"].as<std::string>();
                share["permission"] = row["permission"].as<std::string>();
                share["created_at"] = row["created_at"].as<std::string>();
                
                shares.append(share);
            }
            
            data["shares"] = shares;
            data["total"] = static_cast<int>(result.size());
            
            callback(createApiResponse(200, "获取成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "查询共享列表错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败"));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "获取共享列表异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void ShareController::updateShare(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback,
                                int genealogy_id, int user_id) {
    try {
        // 1. 验证token
        std::string token = getTokenFromHeader(req);
        if (token.empty()) {
            callback(createApiResponse(401, "未提供认证token"));
            return;
        }
        
        int current_user_id = getUserIdFromToken(token);
        if (current_user_id <= 0) {
            callback(createApiResponse(401, "无效的token"));
            return;
        }
        
        // 2. 验证是否是族谱的创建者
        if (!isGenealogyOwner(genealogy_id, current_user_id)) {
            callback(createApiResponse(403, "只有族谱创建者可以更新共享权限"));
            return;
        }
        
        // 3. 解析请求体
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }
        
        if (!json->isMember("permission")) {
            callback(createApiResponse(400, "必须提供permission"));
            return;
        }
        
        std::string permission = (*json)["permission"].asString();
        if (permission != "edit" && permission != "view") {
            callback(createApiResponse(400, "权限只能是 edit 或 view"));
            return;
        }
        
        // 4. 更新共享权限
        auto client = app().getDbClient();
        try {
            client->execSqlSync(
                "UPDATE GenealogyShare SET permission = ? WHERE genealogy_id = ? AND user_id = ?",
                permission, genealogy_id, user_id
            );
            
            callback(createApiResponse(200, "更新权限成功"));
            
        } catch (const std::exception& e) {
            std::cerr << "更新共享权限错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "更新权限失败: " + std::string(e.what())));
        }
        
    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "更新共享权限异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}