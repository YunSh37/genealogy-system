// controllers/AuthController.h
#pragma once
#include <drogon/HttpController.h>
#include <string>
#include <utility>
#include <map>
#include <mutex>

namespace api::v1 {
class AuthController : public drogon::HttpController<AuthController> {
public:
    METHOD_LIST_BEGIN
        ADD_METHOD_TO(AuthController::registerUser, "/api/auth/register", drogon::Post);
        ADD_METHOD_TO(AuthController::login, "/api/auth/login", drogon::Post);
        ADD_METHOD_TO(AuthController::logout, "/api/auth/logout", drogon::Post);
    METHOD_LIST_END

    // 用户注册
    void registerUser(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    
    // 用户登录
    void login(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    
    // 用户注销
    void logout(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    
    // 静态方法：验证token
    static std::pair<int, std::string> validateToken(const std::string& token);
    
    // 静态方法：生成token
    static std::string generateToken(int userId, const std::string& username);
    
    // 静态方法：清除token
    static void removeToken(const std::string& token);
    
private:
    // 验证用户名格式
    bool isValidUsername(const std::string& username);
    
    // 验证密码格式
    bool isValidPassword(const std::string& password);
    
    // Token 存储（静态成员，所有控制器共享）
    static std::map<std::string, std::pair<int, std::string>> tokenStore;  // token -> (user_id, username)
    static std::mutex tokenMutex;
    
    // 随机字符串生成
    static std::string generateRandomString(int length);
};
}