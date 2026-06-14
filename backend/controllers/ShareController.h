// controllers/ShareController.h
#pragma once
#include <drogon/HttpController.h>

namespace api::v1 {
class ShareController : public drogon::HttpController<ShareController> {
public:
    METHOD_LIST_BEGIN
        // 添加共享用户
        ADD_METHOD_TO(ShareController::addShare, "/api/genealogy/{genealogy_id}/share", drogon::Post);
        // 删除共享用户
        ADD_METHOD_TO(ShareController::removeShare, "/api/genealogy/{genealogy_id}/share/{user_id}", drogon::Delete);
        // 获取共享用户列表
        ADD_METHOD_TO(ShareController::getShareList, "/api/genealogy/{genealogy_id}/share", drogon::Get);
        // 更新共享权限
        ADD_METHOD_TO(ShareController::updateShare, "/api/genealogy/{genealogy_id}/share/{user_id}", drogon::Put);
    METHOD_LIST_END

    // 添加共享用户
    void addShare(const drogon::HttpRequestPtr& req,
                 std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                 int genealogy_id);
    
    // 删除共享用户
    void removeShare(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    int genealogy_id, int user_id);
    
    // 获取共享用户列表
    void getShareList(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     int genealogy_id);
    
    // 更新共享权限
    void updateShare(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    int genealogy_id, int user_id);
    
private:
    // 验证用户身份
    int getUserIdFromToken(const std::string& token);
    
    // 检查是否是族谱的创建者
    bool isGenealogyOwner(int genealogy_id, int user_id);
    
    // 检查用户是否存在
    bool userExists(int user_id);
    
    // 检查是否已经在共享列表中
    bool isUserShared(int genealogy_id, int user_id);
    
    // 获取请求头中的token
    std::string getTokenFromHeader(const drogon::HttpRequestPtr& req);
};
}