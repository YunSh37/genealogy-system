// controllers/AuthController.cc
#include "AuthController.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>
#include <regex>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace api::v1;
using namespace drogon;
using Json::Value;

// 初始化静态成员
std::map<std::string, std::pair<int, std::string>> AuthController::tokenStore;
std::mutex AuthController::tokenMutex;

// 辅助函数：创建API响应
static HttpResponsePtr createApiResponse(int code, const std::string& message, const Value& data = Value()) {
    Value json;
    json["code"] = code;
    json["message"] = message;
    if (!data.isNull()) {
        json["data"] = data;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(code == 200 ? k200OK : (code == 400 ? k400BadRequest : 
                        (code == 401 ? k401Unauthorized : k500InternalServerError)));
    return resp;
}

// 生成随机字符串
std::string AuthController::generateRandomString(int length) {
    const std::string charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0, charset.size() - 1);
    
    std::string randomString;
    for (int i = 0; i < length; ++i) {
        randomString += charset[distribution(generator)];
    }
    return randomString;
}

// 生成 Token
std::string AuthController::generateToken(int userId, const std::string& username) {
    // 获取当前时间戳（毫秒）
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto epoch = now_ms.time_since_epoch();
    auto value = std::chrono::duration_cast<std::chrono::milliseconds>(epoch);
    long timestamp = value.count();
    
    // 生成随机字符串
    std::string randomStr = generateRandomString(16);
    
    // 构建 Token
    std::stringstream tokenStream;
    // tokenStream << "genealogy_" 
    //             << timestamp << "_" 
    //             << userId << "_" 
    //             << username << "_" 
    //             << randomStr;
    tokenStream << "genealogy_" << username;
    
    std::string token = tokenStream.str();
    
    // 存储 Token
    {
        std::lock_guard<std::mutex> lock(tokenMutex);
        tokenStore[token] = {userId, username};
    }
    
    std::cout << "生成 Token: " << token << std::endl;
    return token;
}

// 验证 Token
std::pair<int, std::string> AuthController::validateToken(const std::string& token) {
    if (token.empty()) {
        return {0, ""};
    }
    
    std::lock_guard<std::mutex> lock(tokenMutex);
    auto it = tokenStore.find(token);
    if (it == tokenStore.end()) {
        std::cout << "Token 验证失败: 未找到 token" << std::endl;
        return {0, ""};  // 无效token
    }
    
    std::cout << "Token 验证成功: user_id=" << it->second.first 
              << ", username=" << it->second.second << std::endl;
    return it->second;  // 返回 (user_id, username)
}

// 移除 Token
void AuthController::removeToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(tokenMutex);
    auto it = tokenStore.find(token);
    if (it != tokenStore.end()) {
        std::cout << "移除 Token: " << token 
                  << " (user_id=" << it->second.first << ")" << std::endl;
        tokenStore.erase(it);
    }
}

bool AuthController::isValidUsername(const std::string& username) {
    // 用户名长度3-50，只能包含字母、数字、下划线
    std::regex pattern("^[a-zA-Z0-9_]{3,50}$");
    return std::regex_match(username, pattern);
}

bool AuthController::isValidPassword(const std::string& password) {
    // 密码长度6-50
    return password.length() >= 6 && password.length() <= 50;
}

void AuthController::registerUser(const HttpRequestPtr& req,
                                 std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }

        if (!json->isMember("username") || !json->isMember("password")) {
            callback(createApiResponse(400, "必须提供用户名和密码"));
            return;
        }

        std::string username = (*json)["username"].asString();
        std::string password = (*json)["password"].asString();

        if (!isValidUsername(username)) {
            callback(createApiResponse(400, "用户名格式无效（3-50位字母、数字、下划线）"));
            return;
        }
        
        if (!isValidPassword(password)) {
            callback(createApiResponse(400, "密码长度需在6-50位之间"));
            return;
        }

        auto client = app().getDbClient();
        
        // 检查用户名是否已存在
        try {
            auto result = client->execSqlSync("SELECT * FROM User WHERE username = ?", username);
            if (!result.empty()) {
                callback(createApiResponse(400, "用户名已存在"));
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "查询用户错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "数据库查询失败"));
            return;
        }

        // 插入新用户
        try {
            client->execSqlSync("INSERT INTO User (username, password) VALUES (?, ?)", username, password);
            
            Value data;
            data["username"] = username;
            callback(createApiResponse(200, "注册成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "注册用户错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "注册失败: " + std::string(e.what())));
        }

    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "注册异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void AuthController::login(const HttpRequestPtr& req,
                          std::function<void(const HttpResponsePtr&)>&& callback) {
    try {
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }

        if (!json->isMember("username") || !json->isMember("password")) {
            callback(createApiResponse(400, "必须提供用户名和密码"));
            return;
        }

        std::string username = (*json)["username"].asString();
        std::string password = (*json)["password"].asString();

        auto client = app().getDbClient();
        
        try {
            // 查询用户，检查状态是否为 active
            auto result = client->execSqlSync(
                "SELECT * FROM User WHERE username = ? AND password = ? AND status = 'active'",
                username, password
            );

            if (result.empty()) {
                callback(createApiResponse(401, "用户名或密码错误或账户已注销"));
                return;
            }

            // 登录成功
            Value data;
            Value user;
            
            int userId = result[0]["user_id"].as<int>();
            user["user_id"] = userId;
            user["username"] = username;
            
            // 使用专门的函数生成 Token
            std::string token = generateToken(userId, username);
            
            data["token"] = token;
            data["user"] = user;
            
            callback(createApiResponse(200, "登录成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "登录查询错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "登录失败"));
        }

    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "登录异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

void AuthController::logout(const HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto json = req->getJsonObject();
        if (!json) {
            callback(createApiResponse(400, "请求体必须是有效的JSON"));
            return;
        }

        if (!json->isMember("token")) {
            callback(createApiResponse(400, "必须提供token"));
            return;
        }

        std::string token = (*json)["token"].asString();
        
        // 验证 token
        auto [userId, username] = AuthController::validateToken(token);
        if (userId == 0) {
            callback(createApiResponse(401, "无效的token或已过期"));
            return;
        }

        auto client = app().getDbClient();
        
        try {
            // 更新用户状态为 inactive
            client->execSqlSync(
                "UPDATE User SET status = 'inactive' WHERE user_id = ?",
                userId
            );
            
            // 使用专门的函数移除 Token
            AuthController::removeToken(token);
            
            Value data;
            data["message"] = "账户已成功注销，无法再次登录";
            data["user_id"] = userId;
            
            callback(createApiResponse(200, "注销成功", data));
            
        } catch (const std::exception& e) {
            std::cerr << "注销用户错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "注销失败: " + std::string(e.what())));
        }

    } catch (const Json::Exception& e) {
        callback(createApiResponse(400, "JSON解析错误: " + std::string(e.what())));
    } catch (const std::exception& e) {
        std::cerr << "注销异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}