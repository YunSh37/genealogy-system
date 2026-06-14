// controllers/DashboardController.h
#pragma once
#include <drogon/HttpController.h>

namespace api::v1 {
class DashboardController : public drogon::HttpController<DashboardController> {
public:
    METHOD_LIST_BEGIN
        // 获取族谱统计信息
        ADD_METHOD_TO(DashboardController::getGenealogyStats, "/api/genealogy/{genealogy_id}/stats", drogon::Get);
    METHOD_LIST_END

    // 获取族谱统计信息
    void getGenealogyStats(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          int genealogy_id);
    
private:
    // 验证用户身份
    int getUserIdFromToken(const std::string& token);
    
    // 验证对族谱的权限
    bool checkGenealogyAccess(int genealogy_id, int user_id);
    
    // 获取请求头中的token
    std::string getTokenFromHeader(const drogon::HttpRequestPtr& req);
};
}