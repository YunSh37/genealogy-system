// controllers/RelationshipController.h
#pragma once
#include <drogon/HttpController.h>
#include <string>
#include <vector>

namespace api::v1 {
class RelationshipController : public drogon::HttpController<RelationshipController> {
public:
    METHOD_LIST_BEGIN
        // 查询祖先
        ADD_METHOD_TO(RelationshipController::getAncestors, "/api/member/{member_id}/ancestors", drogon::Get);
        // 查询后代
        ADD_METHOD_TO(RelationshipController::getDescendants, "/api/member/{member_id}/descendants", drogon::Get);
        // 查询亲缘关系
        ADD_METHOD_TO(RelationshipController::getRelationship, "/api/member/relationship", drogon::Get);
        // 获取家族树
        ADD_METHOD_TO(RelationshipController::getFamilyTree, "/api/member/{member_id}/family-tree", drogon::Get);
        // 分层树加载（族谱树性能优化）
        ADD_METHOD_TO(RelationshipController::getTreeLayers, "/api/genealogy/{genealogy_id}/tree-layers", drogon::Get);
    METHOD_LIST_END

    // 查询祖先
    void getAncestors(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     int member_id);

    // 查询后代
    void getDescendants(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       int member_id);

    // 查询亲缘关系
    void getRelationship(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    // 获取家族树
    void getFamilyTree(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      int member_id);

    // 分层树加载
    void getTreeLayers(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                       int genealogy_id);
    
private:
    // 验证用户身份
    int getUserIdFromToken(const std::string& token);
    
    // 验证对成员的权限
    bool checkMemberAccess(int member_id, int user_id);

    // 获取成员的配偶ID
    int getSpouseId(int member_id);
    
    // 获取包含配偶的亲属路径
    std::vector<int> getAncestorPathWithSpouse(int member_id, int max_depth);

    // 获取两个成员的最近公共祖先
    int findCommonAncestor(int member_id1, int member_id2);
    
    // 递归获取祖先路径
    std::vector<int> getAncestorPath(int member_id, int max_depth = 10);

    // 递归获取后代路径
    std::vector<int> getDescendantPath(int member_id, int max_depth = 10);

    // 获取祖先路径（线性链，优先父系，追溯到根节点）
    std::vector<int> getAncestorLineagePath(int member_id);
    
    // 获取成员详情
    Json::Value getMemberDetails(int member_id);
    
    // 获取请求头中的token
    std::string getTokenFromHeader(const drogon::HttpRequestPtr& req);
};
}