// controllers/DashboardController.cc
#include "DashboardController.h"
#include "AuthController.h"
#include "ResponseUtils.h"
#include "models/DbUtils.h"
#include "models/StatsCache.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>
#include <cmath>
#include <map>

using namespace api::v1;
using namespace drogon;
using Json::Value;
using myutils::createApiResponse;


std::string DashboardController::getTokenFromHeader(const HttpRequestPtr& req) {
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0) {
        return authHeader.substr(7);
    }
    return "";
}

int DashboardController::getUserIdFromToken(const std::string& token) {
    if (token.empty()) {
        return 0;
    }
    
    auto [userId, username] = AuthController::validateToken(token);
    return userId;
}

bool DashboardController::checkGenealogyAccess(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        // 检查是否是创建者
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        if (!result.empty()) {
            return true;  // 创建者
        }
        
        // 检查是否在共享列表中
        auto shareResult = client->execSqlSync(
            "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );
        
        return !shareResult.empty();  // 共享用户
    } catch (const std::exception& e) {
        std::cerr << "检查族谱权限错误: " << e.what() << std::endl;
        return false;
    }
}

void DashboardController::getGenealogyStats(const HttpRequestPtr& req,
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
        
        // 2. 验证对族谱的权限
        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权查看此族谱"));
            return;
        }
        
        // 3. 获取统计信息（Phase 3: 内存缓存优化）
        auto client = app().getDbClient();
        try {
            // Phase 3: 缓存命中则直接返回（TTL 30 分钟）
            std::string cache_key = "dashboard_" + std::to_string(genealogy_id);
            Json::Value cached_data;
            if (cache::StatsCache::instance().get(cache_key, cached_data)) {
                auto resp = createApiResponse(200, "获取成功", cached_data);
                resp->addHeader("Cache-Control", "public, max-age=300");
                resp->addHeader("X-Cache", "HIT");
                callback(resp);
                return;
            }

            // 查询总人数和男女数量
            auto result = client->execSqlSync(
                "SELECT "
                "COUNT(*) as total, "
                "SUM(CASE WHEN gender = 'male' THEN 1 ELSE 0 END) as male_count, "
                "SUM(CASE WHEN gender = 'female' THEN 1 ELSE 0 END) as female_count "
                "FROM Member WHERE genealogy_id = ?",
                genealogy_id
            );
            
            Value data;
            data["genealogy_id"] = genealogy_id;
            
            if (!result.empty()) {
                int total = result[0]["total"].as<int>();
                int male_count = result[0]["male_count"].as<int>();
                int female_count = result[0]["female_count"].as<int>();
                
                data["total_members"] = total;
                
                // 性别统计
                Value gender_stats;
                gender_stats["male"] = male_count;
                gender_stats["female"] = female_count;
                
                // 计算比例（百分比，保留一位小数）
                if (total > 0) {
                    double male_percentage = std::round((static_cast<double>(male_count) * 1000 / total)) / 10;
                    double female_percentage = std::round((static_cast<double>(female_count) * 1000 / total)) / 10;
                    
                    gender_stats["male_percentage"] = male_percentage;
                    gender_stats["female_percentage"] = female_percentage;
                } else {
                    gender_stats["male_percentage"] = 0.0;
                    gender_stats["female_percentage"] = 0.0;
                }
                
                data["gender_stats"] = gender_stats;
            } else {
                data["total_members"] = 0;
                Value gender_stats;
                gender_stats["male"] = 0;
                gender_stats["female"] = 0;
                gender_stats["male_percentage"] = 0.0;
                gender_stats["female_percentage"] = 0.0;
                data["gender_stats"] = gender_stats;
            }

            // 辈分分布统计（generation_stats）
            try {
                auto genResult = client->execSqlSync(R"(
                    SELECT generation, gender, COUNT(*) as cnt
                    FROM Member
                    WHERE genealogy_id = ? AND generation IS NOT NULL AND generation > 0
                    GROUP BY generation, gender
                    ORDER BY generation
                )", genealogy_id);

                // 聚合：每代人数 + 男女数
                std::map<int, std::pair<int, int>> genMap;  // generation -> (male, female)
                for (const auto& row : genResult) {
                    int gen = row["generation"].as<int>();
                    std::string gender = row["gender"].as<std::string>();
                    int cnt = row["cnt"].as<int>();
                    if (gender == "male") genMap[gen].first = cnt;
                    else genMap[gen].second = cnt;
                }

                Value genStats(Json::arrayValue);
                for (const auto& [gen, counts] : genMap) {
                    Value item;
                    item["generation"] = gen;
                    item["male"] = counts.first;
                    item["female"] = counts.second;
                    item["count"] = counts.first + counts.second;
                    genStats.append(item);
                }
                data["generation_stats"] = genStats;

            } catch (const std::exception& e) {
                std::cerr << "获取辈分统计错误: " << e.what() << std::endl;
                data["generation_stats"] = Json::arrayValue;
            }

            // Phase 3: 缓存结果（TTL 30 分钟）
            cache::StatsCache::instance().set(cache_key, data, 1800);

            auto resp = createApiResponse(200, "获取成功", data);
            resp->addHeader("Cache-Control", "public, max-age=300");
            resp->addHeader("X-Cache", "MISS");
            callback(resp);
            
        } catch (const std::exception& e) {
            std::cerr << "获取统计信息错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "统计失败: " + std::string(e.what())));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "获取统计信息异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}