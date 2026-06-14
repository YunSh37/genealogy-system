// controllers/MemberController.h
#pragma once
#include <drogon/HttpController.h>
#include <string>
#include <vector>

namespace api::v1 {
class MemberController : public drogon::HttpController<MemberController> {
public:
    METHOD_LIST_BEGIN
        // 添加成员
        ADD_METHOD_TO(MemberController::createMember, "/api/genealogy/{genealogy_id}/member", drogon::Post);
        // 获取成员列表
        ADD_METHOD_TO(MemberController::getMemberList, "/api/genealogy/{genealogy_id}/member", drogon::Get);
        // 获取成员详情
        ADD_METHOD_TO(MemberController::getMemberDetail, "/api/member/{member_id}", drogon::Get);
        // 更新成员信息
        ADD_METHOD_TO(MemberController::updateMember, "/api/member/{member_id}", drogon::Put);
        // 删除成员
        ADD_METHOD_TO(MemberController::deleteMember, "/api/member/{member_id}", drogon::Delete);
        // 搜索成员
        ADD_METHOD_TO(MemberController::searchMember, "/api/member/search", drogon::Get);
        // 家族树专用接口（无分页，精简字段）
        ADD_METHOD_TO(MemberController::getTreeData, "/api/genealogy/{genealogy_id}/tree", drogon::Get);
        // 批量获取成员详情
        ADD_METHOD_TO(MemberController::getMembersBatch, "/api/member/batch", drogon::Post);
    METHOD_LIST_END

    // 添加成员
    void createMember(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     int genealogy_id);
    
    // 获取成员列表
    void getMemberList(const drogon::HttpRequestPtr& req,
                      std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                      int genealogy_id);
    
    // 获取成员详情
    void getMemberDetail(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        int member_id);
    
    // 更新成员信息
    void updateMember(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     int member_id);
    
    // 删除成员
    void deleteMember(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                     int member_id);
    
    // 搜索成员
    void searchMember(const drogon::HttpRequestPtr& req,
                     std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    // 家族树专用接口（无分页，精简字段）
    void getTreeData(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                    int genealogy_id);

    // 批量获取成员详情
    void getMembersBatch(const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

private:
    // 验证用户身份
    int getUserIdFromToken(const std::string& token);
    
    // 验证用户对族谱的权限
    bool checkGenealogyAccess(int genealogy_id, int user_id);
    
    // 验证用户对成员的权限
    bool checkMemberAccess(int member_id, int user_id);
    
    // 验证性别格式
    bool isValidGender(const std::string& gender);
    
    // 验证日期格式
    bool isValidDate(const std::string& date_str);
    
    // 验证父母ID是否存在且属于同一族谱
    bool validateParentIds(int genealogy_id, int father_id, int mother_id);
    
    // 验证成员是否能被删除（检查是否有后代）
    bool canDeleteMember(int member_id);
    
    // 获取请求头中的token
    std::string getTokenFromHeader(const drogon::HttpRequestPtr& req);
};
}