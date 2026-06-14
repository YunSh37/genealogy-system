// controllers/GenealogyController.h
#pragma once
#include <drogon/HttpController.h>

namespace api::v1 {
class GenealogyController : public drogon::HttpController<GenealogyController> {
public:
    METHOD_LIST_BEGIN
        // 创建族谱
        ADD_METHOD_TO(GenealogyController::createGenealogy, "/api/genealogy", drogon::Post);
        // 获取族谱列表
        ADD_METHOD_TO(GenealogyController::getGenealogyList, "/api/genealogy", drogon::Get);
        // 获取族谱详情
        ADD_METHOD_TO(GenealogyController::getGenealogyDetail, "/api/genealogy/{genealogy_id}", drogon::Get);
        // 更新族谱信息
        ADD_METHOD_TO(GenealogyController::updateGenealogy, "/api/genealogy/{genealogy_id}", drogon::Put);
        // 删除族谱
        ADD_METHOD_TO(GenealogyController::deleteGenealogy, "/api/genealogy/{genealogy_id}", drogon::Delete);
    METHOD_LIST_END

    // 创建族谱
    void createGenealogy(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    
    // 获取族谱列表
    void getGenealogyList(const drogon::HttpRequestPtr& req,
                         std::function<void(const drogon::HttpResponsePtr&)>&& callback);
    
    // 获取族谱详情
    void getGenealogyDetail(const drogon::HttpRequestPtr& req,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                           int genealogy_id);
    
    // 更新族谱信息
    void updateGenealogy(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        int genealogy_id);
    
    // 删除族谱
    void deleteGenealogy(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        int genealogy_id);
    
    bool hasEditPermission(int genealogy_id, int user_id);
    bool hasViewPermission(int genealogy_id, int user_id);
    std::string getUserRole(int genealogy_id, int user_id);
    
private:
    // 验证用户身份（从token获取user_id）
    int getUserIdFromToken(const std::string& token);
    
    // 验证族谱所有权
    bool checkGenealogyOwnership(int genealogy_id, int user_id);
    
    // 验证族谱名称是否重复
    bool isGenealogyNameExists(const std::string& name, int user_id, int exclude_id = 0);
    
    // 验证必填字段
    bool validateRequiredFields(const Json::Value& json, const std::vector<std::string>& fields);
};
}