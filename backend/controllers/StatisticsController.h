#pragma once
#include <drogon/HttpController.h>

namespace api::v1 {
class StatisticsController : public drogon::HttpController<StatisticsController> {
public:
    METHOD_LIST_BEGIN
        // 平均寿命最长的一代
        ADD_METHOD_TO(StatisticsController::getLongestLivedGeneration, 
                     "/api/genealogy/{genealogy_id}/stats/longest-lived-generation", 
                     drogon::Get);
        
        // 年龄超过50岁且无配偶的男性
        ADD_METHOD_TO(StatisticsController::getUnmarriedMenOver50, 
                     "/api/genealogy/{genealogy_id}/stats/unmarried-men-over-50", 
                     drogon::Get);
        
        // 出生早于平均出生年份的成员
        ADD_METHOD_TO(StatisticsController::getMembersBornBeforeAverage, 
                     "/api/genealogy/{genealogy_id}/stats/born-before-average", 
                     drogon::Get);
        
        // SQL核心功能演示接口
        ADD_METHOD_TO(StatisticsController::getCoreQueriesDemo, 
                     "/api/genealogy/{genealogy_id}/sql-demo", 
                     drogon::Get);
    METHOD_LIST_END

    // 平均寿命最长的一代
    void getLongestLivedGeneration(const drogon::HttpRequestPtr& req,
                                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                 int genealogy_id);
    
    // 年龄超过50岁且无配偶的男性
    void getUnmarriedMenOver50(const drogon::HttpRequestPtr& req,
                             std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                             int genealogy_id);
    
    // 出生早于平均出生年份的成员
    void getMembersBornBeforeAverage(const drogon::HttpRequestPtr& req,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                   int genealogy_id);
    
    // SQL核心功能演示
    void getCoreQueriesDemo(const drogon::HttpRequestPtr& req,
                          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                          int genealogy_id);
    
private:
    // 验证用户身份
    int getUserIdFromToken(const std::string& token);
    
    // 验证对族谱的权限
    bool checkGenealogyAccess(int genealogy_id, int user_id);
    
    // 获取请求头中的token
    std::string getTokenFromHeader(const drogon::HttpRequestPtr& req);
    
    // 计算年龄
    int calculateAge(const std::string& birth_date);
    
    // 计算寿命
    int calculateLifespan(const std::string& birth_date, const std::string& death_date);

};
}