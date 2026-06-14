#include "StatisticsController.h"
#include "AuthController.h"
#include "ResponseUtils.h"
#include "models/Member.h"
#include "models/DbUtils.h"
#include "models/StatsCache.h"
#include <drogon/drogon.h>
#include <json/json.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <climits>
#include <unordered_set>
#include <set>

using namespace api::v1;
using namespace drogon;
using Json::Value;
using myutils::createApiResponse;

// 辅助函数
std::string StatisticsController::getTokenFromHeader(const HttpRequestPtr& req) {
    auto authHeader = req->getHeader("Authorization");
    if (!authHeader.empty() && authHeader.find("Bearer ") == 0) {
        return authHeader.substr(7);
    }
    return "";
}

int StatisticsController::getUserIdFromToken(const std::string& token) {
    if (token.empty()) {
        return 0;
    }

    auto [userId, username] = AuthController::validateToken(token);
    return userId;
}

bool StatisticsController::checkGenealogyAccess(int genealogy_id, int user_id) {
    auto client = app().getDbClient();
    try {
        auto result = client->execSqlSync(
            "SELECT user_id FROM Genealogy WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );

        if (!result.empty()) {
            return true;
        }

        auto shareResult = client->execSqlSync(
            "SELECT permission FROM GenealogyShare WHERE genealogy_id = ? AND user_id = ?",
            genealogy_id, user_id
        );

        return !shareResult.empty();
    } catch (const std::exception& e) {
        std::cerr << "检查族谱权限错误: " << e.what() << std::endl;
        return false;
    }
}

int StatisticsController::calculateAge(const std::string& birth_date) {
    if (birth_date.empty()) return 0;

    try {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm = *std::localtime(&now_time);

        int birth_year, birth_month, birth_day;
        std::sscanf(birth_date.c_str(), "%d-%d-%d", &birth_year, &birth_month, &birth_day);

        int age = now_tm.tm_year + 1900 - birth_year;

        if (now_tm.tm_mon + 1 < birth_month) age--;
        else if (now_tm.tm_mon + 1 == birth_month && now_tm.tm_mday < birth_day) age--;

        return age;
    } catch (...) {
        return 0;
    }
}

int StatisticsController::calculateLifespan(const std::string& birth_date, const std::string& death_date) {
    if (birth_date.empty() || death_date.empty()) return 0;

    try {
        int birth_year, birth_month, birth_day;
        int death_year, death_month, death_day;

        std::sscanf(birth_date.c_str(), "%d-%d-%d", &birth_year, &birth_month, &birth_day);
        std::sscanf(death_date.c_str(), "%d-%d-%d", &death_year, &death_month, &death_day);

        int lifespan = death_year - birth_year;

        if (death_month < birth_month) lifespan--;
        else if (death_month == birth_month && death_day < birth_day) lifespan--;

        return lifespan;
    } catch (...) {
        return 0;
    }
}

// ============================================================
// 年龄超过50岁且无配偶的男性
// 优化：使用 TIMESTAMPDIFF 精确计算年龄
// ============================================================
void StatisticsController::getUnmarriedMenOver50(const HttpRequestPtr& req,
                                               std::function<void(const HttpResponsePtr&)>&& callback,
                                               int genealogy_id) {
    try {
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

        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权查看此族谱"));
            return;
        }

        // Phase 3: 缓存命中则直接返回（TTL 30 分钟）
        std::string cache_key = "stats_unmarried_" + std::to_string(genealogy_id);
        Json::Value cached_data;
        if (cache::StatsCache::instance().get(cache_key, cached_data)) {
            callback(createApiResponse(200, "获取成功", cached_data));
            return;
        }

        auto client = app().getDbClient();
        try {
            // 使用 TIMESTAMPDIFF 精确计算年龄，而非 YEAR(CURDATE()) - YEAR(birth_date)
            std::string sql = R"(
SELECT
    member_id,
    name,
    birth_date,
    TIMESTAMPDIFF(YEAR, birth_date, CURDATE()) as age,
    spouse_id
FROM Member
WHERE genealogy_id = ?
    AND gender = 'male'
    AND (spouse_id IS NULL OR spouse_id <= 0)
    AND birth_date IS NOT NULL
    AND TIMESTAMPDIFF(YEAR, birth_date, CURDATE()) > 50
ORDER BY age DESC
)";

            auto result = client->execSqlSync(sql, genealogy_id);

            Value data;
            data["genealogy_id"] = genealogy_id;
            data["query_condition"] = "年龄超过50岁且无配偶的男性成员";

            Value membersArray(Json::arrayValue);
            for (const auto& row : result) {
                Value member;
                member["member_id"] = row["member_id"].as<int>();
                member["name"] = row["name"].as<std::string>();
                member["birth_date"] = dbutils::safeStr(row, "birth_date");
                member["age"] = row["age"].as<int>();
                member["spouse_id"] = dbutils::safeInt(row, "spouse_id");
                membersArray.append(member);
            }

            data["members"] = membersArray;
            data["total"] = static_cast<int>(membersArray.size());

            // 统计信息
            Value stats;
            int min_age = 0, max_age = 0, total_age = 0;
            if (!membersArray.empty()) {
                min_age = membersArray[0]["age"].asInt();
                max_age = membersArray[0]["age"].asInt();
                for (const auto& member : membersArray) {
                    int age = member["age"].asInt();
                    if (age < min_age) min_age = age;
                    if (age > max_age) max_age = age;
                    total_age += age;
                }
            }
            stats["min_age"] = min_age;
            stats["max_age"] = max_age;
            stats["avg_age"] = membersArray.empty() ? 0.0 : static_cast<double>(total_age) / membersArray.size();
            data["statistics"] = stats;

            // Phase 3: 缓存结果（TTL 30 分钟）
            cache::StatsCache::instance().set(cache_key, data, 1800);

            callback(createApiResponse(200, "获取成功", data));

        } catch (const std::exception& e) {
            std::cerr << "查询年龄超过50岁男性错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    }catch (const std::exception& e) {
        std::cerr << "查询年龄超过50岁男性异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// ============================================================
// SQL核心功能演示
// ============================================================
void StatisticsController::getCoreQueriesDemo(const drogon::HttpRequestPtr& req,
                                            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                            int genealogy_id) {
    try {
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

        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权查看此族谱"));
            return;
        }

        auto member_id_str = req->getParameter("member_id");
        if (member_id_str.empty()) {
            callback(createApiResponse(400, "必须提供member_id参数"));
            return;
        }

        int member_id;
        try {
            member_id = std::stoi(member_id_str);
        } catch (...) {
            callback(createApiResponse(400, "成员ID参数无效"));
            return;
        }

        auto client = app().getDbClient();
        auto checkResult = client->execSqlSync(
            "SELECT genealogy_id FROM Member WHERE member_id = ?",
            member_id
        );

        if (checkResult.empty() || checkResult[0]["genealogy_id"].as<int>() != genealogy_id) {
            callback(createApiResponse(403, "该成员不属于指定族谱"));
            return;
        }

        try {
            Value data;
            data["genealogy_id"] = genealogy_id;
            data["member_id"] = member_id;

            // 获取成员基本信息
            auto memberInfo = client->execSqlSync(
                "SELECT name, gender, birth_date, spouse_id FROM Member WHERE member_id = ?",
                member_id
            );

            if (!memberInfo.empty()) {
                data["member_name"] = memberInfo[0]["name"].as<std::string>();
                data["member_gender"] = memberInfo[0]["gender"].as<std::string>();
                data["member_birth_date"] = dbutils::safeStr(memberInfo[0], "birth_date");
            }

            // =========================================
            // 1. 基本查询：给定成员ID，查询其配偶及所有子女
            // 实验要求 SQL 1: 基本查询
            // =========================================
            {
                // 查询配偶
                auto spouseResult = client->execSqlSync(
                    "SELECT m2.member_id, m2.name, m2.gender, m2.birth_date "
                    "FROM Member m1 "
                    "LEFT JOIN Member m2 ON m1.spouse_id = m2.member_id "
                    "WHERE m1.member_id = ?",
                    member_id
                );

                // 查询所有子女（通过 father_id 或 mother_id）
                auto childrenResult = client->execSqlSync(
                    "SELECT member_id, name, gender, birth_date "
                    "FROM Member "
                    "WHERE (father_id = ? OR mother_id = ?) AND genealogy_id = ? "
                    "ORDER BY birth_date",
                    member_id, member_id, genealogy_id
                );

                Value spouse(Json::objectValue);
                if (!spouseResult.empty() && !spouseResult[0]["member_id"].isNull()) {
                    spouse["member_id"] = spouseResult[0]["member_id"].as<int>();
                    spouse["name"] = spouseResult[0]["name"].as<std::string>();
                    spouse["gender"] = spouseResult[0]["gender"].as<std::string>();
                    spouse["birth_date"] = spouseResult[0]["birth_date"].as<std::string>();
                }

                Value childrenArray(Json::arrayValue);
                for (const auto& row : childrenResult) {
                    Value child(Json::objectValue);
                    child["member_id"] = row["member_id"].as<int>();
                    child["name"] = row["name"].as<std::string>();
                    child["gender"] = row["gender"].as<std::string>();
                    child["birth_date"] = dbutils::safeStr(row, "birth_date");
                    childrenArray.append(child);
                }

                Value basicQuery(Json::objectValue);
                basicQuery["spouse"] = spouse;
                basicQuery["children"] = childrenArray;
                basicQuery["children_count"] = static_cast<int>(childrenArray.size());
                data["basic_query"] = basicQuery;
            }

            // =========================================
            // 2. 递归查询（核心）：使用 Recursive CTE 查询所有历代祖先
            // 实验要求 SQL 2: 递归查询
            // =========================================
            {
                std::string ancestorsSql = R"(
WITH RECURSIVE ancestors AS (
    SELECT member_id, name, gender, birth_date, father_id, mother_id, 0 as level
    FROM Member WHERE member_id = ?

    UNION ALL

    SELECT m.member_id, m.name, m.gender, m.birth_date, m.father_id, m.mother_id, a.level + 1
    FROM Member m
    INNER JOIN ancestors a ON (m.member_id = a.father_id OR m.member_id = a.mother_id)
    WHERE a.father_id IS NOT NULL OR a.mother_id IS NOT NULL
)
SELECT member_id, name, gender, birth_date, level
FROM ancestors
WHERE level > 0
ORDER BY level
)";

                auto ancestorsResult = client->execSqlSync(ancestorsSql, member_id);

                Value ancestorsArray(Json::arrayValue);
                for (const auto& row : ancestorsResult) {
                    Value ancestor(Json::objectValue);
                    ancestor["member_id"] = row["member_id"].as<int>();
                    ancestor["name"] = row["name"].as<std::string>();
                    ancestor["gender"] = row["gender"].as<std::string>();
                    ancestor["birth_date"] = dbutils::safeStr(row, "birth_date");
                    ancestor["generation"] = row["level"].as<int>();
                    ancestorsArray.append(ancestor);
                }

                data["ancestors"] = ancestorsArray;
                data["ancestors_count"] = static_cast<int>(ancestorsArray.size());
            }

            // =========================================
            // 3. 统计查询：平均寿命最长的一代人（辈分）
            // 实验要求 SQL 3: 按 generation 分组统计平均寿命
            // =========================================
            {
                std::string lifespanSql = R"(
SELECT
    generation,
    COUNT(*) as member_count,
    AVG(TIMESTAMPDIFF(YEAR, birth_date, death_date)) as avg_lifespan
FROM Member
WHERE genealogy_id = ?
    AND birth_date IS NOT NULL
    AND death_date IS NOT NULL
    AND generation IS NOT NULL
    AND generation > 0
GROUP BY generation
HAVING COUNT(*) >= 1
ORDER BY avg_lifespan DESC
LIMIT 1
)";

                auto lifespanResult = client->execSqlSync(lifespanSql, genealogy_id);

                Value lifespanQuery(Json::objectValue);
                if (!lifespanResult.empty()) {
                    lifespanQuery["generation"] = lifespanResult[0]["generation"].as<int>();
                    lifespanQuery["avg_lifespan"] = lifespanResult[0]["avg_lifespan"].as<double>();
                    lifespanQuery["member_count"] = lifespanResult[0]["member_count"].as<int>();
                } else {
                    lifespanQuery["generation"] = 0;
                    lifespanQuery["avg_lifespan"] = 0.0;
                    lifespanQuery["member_count"] = 0;
                }
                data["longest_lived_generation"] = lifespanQuery;
            }

            // =========================================
            // 4. 查询年龄超过50岁且无配偶的男性成员
            // 实验要求 SQL 4: 索引友好查询（直接用 birth_date 比 YEAR()）
            // =========================================
            {
                std::string unmarriedSql = R"(
SELECT
    member_id, name, birth_date,
    TIMESTAMPDIFF(YEAR, birth_date, CURDATE()) as age
FROM Member
WHERE genealogy_id = ?
    AND gender = 'male'
    AND (spouse_id IS NULL OR spouse_id <= 0)
    AND birth_date IS NOT NULL
    AND TIMESTAMPDIFF(YEAR, birth_date, CURDATE()) > 50
ORDER BY age DESC
)";

                auto unmarriedResult = client->execSqlSync(unmarriedSql, genealogy_id);

                Value unmarriedArray(Json::arrayValue);
                for (const auto& row : unmarriedResult) {
                    Value m(Json::objectValue);
                    m["member_id"] = row["member_id"].as<int>();
                    m["name"] = row["name"].as<std::string>();
                    m["birth_date"] = dbutils::safeStr(row, "birth_date");
                    m["age"] = row["age"].as<int>();
                    unmarriedArray.append(m);
                }

                Value unmarriedQuery(Json::objectValue);
                unmarriedQuery["members"] = unmarriedArray;
                unmarriedQuery["total"] = static_cast<int>(unmarriedArray.size());
                data["unmarried_men_over_50"] = unmarriedQuery;
            }

            // =========================================
            // 5. 出生日期早于该辈分平均出生年份的成员
            // 实验要求 SQL 5: 依赖 generation 列的 JOIN 查询
            // =========================================
            {
                std::string bornBeforeAvgSql = R"(
SELECT
    m.member_id, m.name, m.birth_date,
    YEAR(m.birth_date) as birth_year,
    m.generation,
    gen_avg.avg_birth_year,
    ROUND(gen_avg.avg_birth_year - YEAR(m.birth_date), 1) as years_earlier
FROM Member m
JOIN (
    SELECT generation, AVG(YEAR(birth_date)) as avg_birth_year
    FROM Member
    WHERE genealogy_id = ?
        AND birth_date IS NOT NULL
        AND generation IS NOT NULL
        AND generation > 0
    GROUP BY generation
) as gen_avg ON m.generation = gen_avg.generation
WHERE m.genealogy_id = ?
    AND m.birth_date IS NOT NULL
    AND m.generation IS NOT NULL
    AND YEAR(m.birth_date) < gen_avg.avg_birth_year
ORDER BY m.generation, m.birth_date
)";

                auto bornResult = client->execSqlSync(bornBeforeAvgSql, genealogy_id, genealogy_id);

                Value bornArray(Json::arrayValue);
                for (const auto& row : bornResult) {
                    Value m(Json::objectValue);
                    m["member_id"] = row["member_id"].as<int>();
                    m["name"] = row["name"].as<std::string>();
                    m["birth_date"] = dbutils::safeStr(row, "birth_date");
                    m["birth_year"] = row["birth_year"].as<int>();
                    m["generation"] = dbutils::safeInt(row, "generation");
                    m["avg_birth_year"] = row["avg_birth_year"].as<double>();
                    m["years_earlier"] = row["years_earlier"].as<double>();
                    bornArray.append(m);
                }

                Value bornQuery(Json::objectValue);
                bornQuery["members"] = bornArray;
                bornQuery["total"] = static_cast<int>(bornArray.size());
                data["born_before_average"] = bornQuery;
            }

            callback(createApiResponse(200, "SQL核心功能演示成功", data));

        } catch (const std::exception& e) {
            std::cerr << "SQL核心功能演示错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "SQL核心功能演示异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}


// ============================================================
// 出生早于平均出生年份的成员
// 优化：直接使用 generation 列替代递归CTE计算辈分
// 实验要求 SQL 5: 找出家族中"出生年份"早于该辈分平均出生年份的所有成员
// ============================================================
void StatisticsController::getMembersBornBeforeAverage(const HttpRequestPtr& req,
                                                     std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                                     int genealogy_id) {
    try {
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

        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权查看此族谱"));
            return;
        }

        // Phase 3: 缓存命中则直接返回（TTL 30 分钟）
        std::string cache_key = "stats_born_avg_" + std::to_string(genealogy_id);
        Json::Value cached_data;
        if (cache::StatsCache::instance().get(cache_key, cached_data)) {
            callback(createApiResponse(200, "获取成功", cached_data));
            return;
        }

        auto client = app().getDbClient();
        try {
            // 直接使用 generation 列，不再使用递归CTE计算辈分
            // 先计算每代的平均出生年份，再找出早于平均值的成员
            std::string sql = R"(
SELECT
    m.member_id,
    m.name,
    m.birth_date,
    YEAR(m.birth_date) as birth_year,
    m.generation,
    gen_avg.avg_birth_year,
    ROUND(gen_avg.avg_birth_year - YEAR(m.birth_date), 1) as years_earlier
FROM Member m
JOIN (
    SELECT
        generation,
        AVG(YEAR(birth_date)) as avg_birth_year
    FROM Member
    WHERE genealogy_id = ?
        AND birth_date IS NOT NULL
        AND generation IS NOT NULL
        AND generation > 0
    GROUP BY generation
) as gen_avg ON m.generation = gen_avg.generation
WHERE m.genealogy_id = ?
    AND m.birth_date IS NOT NULL
    AND m.generation IS NOT NULL
    AND YEAR(m.birth_date) < gen_avg.avg_birth_year
ORDER BY m.generation, m.birth_date
)";

            auto result = client->execSqlSync(sql, genealogy_id, genealogy_id);

            Value data;
            data["genealogy_id"] = genealogy_id;
            data["query_condition"] = "出生年份早于该辈分平均出生年份的成员";

            Value membersArray(Json::arrayValue);
            for (const auto& row : result) {
                Value member;
                member["member_id"] = row["member_id"].as<int>();
                member["name"] = row["name"].as<std::string>();
                member["birth_date"] = dbutils::safeStr(row, "birth_date");
                member["birth_year"] = row["birth_year"].as<int>();
                member["generation"] = dbutils::safeInt(row, "generation");
                member["avg_birth_year"] = row["avg_birth_year"].as<double>();
                member["years_earlier"] = row["years_earlier"].as<int>();
                membersArray.append(member);
            }

            data["members"] = membersArray;
            data["total"] = static_cast<int>(membersArray.size());

            // 计算统计信息
            Value stats;
            if (!membersArray.empty()) {
                int min_gen = membersArray[0]["generation"].asInt();
                int max_gen = membersArray[0]["generation"].asInt();
                double total_years = 0.0;
                std::set<int> gens;

                for (const auto& member : membersArray) {
                    int gen = member["generation"].asInt();
                    if (gen < min_gen) min_gen = gen;
                    if (gen > max_gen) max_gen = gen;
                    gens.insert(gen);
                    total_years += member["years_earlier"].asInt();
                }

                stats["min_generation"] = min_gen;
                stats["max_generation"] = max_gen;
                stats["generation_count"] = static_cast<int>(gens.size());
                stats["avg_years_earlier"] = total_years / membersArray.size();
            }
            data["statistics"] = stats;

            // Phase 3: 缓存结果（TTL 30 分钟）
            cache::StatsCache::instance().set(cache_key, data, 1800);

            callback(createApiResponse(200, "获取成功", data));

        } catch (const std::exception& e) {
            std::cerr << "查询早于平均出生年份错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "查询失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "查询早于平均出生年份异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}

// ============================================================
// 平均寿命最长的一代
// 优化：直接使用 generation 列替代递归CTE计算辈分
// 实验要求 SQL 3: 统计某个家族中平均寿命最长的一代人（辈分）
// ============================================================
void StatisticsController::getLongestLivedGeneration(const HttpRequestPtr& req,
                                                   std::function<void(const HttpResponsePtr&)>&& callback,
                                                   int genealogy_id) {
    try {
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

        if (!checkGenealogyAccess(genealogy_id, user_id)) {
            callback(createApiResponse(403, "无权查看此族谱"));
            return;
        }

        // Phase 3: 缓存命中则直接返回（TTL 30 分钟）
        std::string cache_key = "stats_lifespan_" + std::to_string(genealogy_id);
        Json::Value cached_data;
        if (cache::StatsCache::instance().get(cache_key, cached_data)) {
            callback(createApiResponse(200, "获取成功", cached_data));
            return;
        }

        auto client = app().getDbClient();
        try {
            // 直接使用 generation 列，不再使用递归CTE
            // 只统计有出生和死亡日期的成员（才能计算寿命）
            std::string sql = R"(
SELECT
    generation,
    COUNT(*) as member_count,
    AVG(TIMESTAMPDIFF(YEAR, birth_date, death_date)) as avg_lifespan
FROM Member
WHERE genealogy_id = ?
    AND birth_date IS NOT NULL
    AND death_date IS NOT NULL
    AND generation IS NOT NULL
    AND generation > 0
GROUP BY generation
HAVING COUNT(*) >= 1
ORDER BY avg_lifespan DESC
LIMIT 1
)";

            auto result = client->execSqlSync(sql, genealogy_id);

            Value data;
            data["genealogy_id"] = genealogy_id;

            if (!result.empty()) {
                int target_generation = result[0]["generation"].as<int>();
                data["generation"] = target_generation;
                data["avg_lifespan"] = result[0]["avg_lifespan"].as<double>();
                data["member_count"] = result[0]["member_count"].as<int>();

                // 获取该代成员的详细信息
                std::string members_sql = R"(
SELECT
    m.member_id,
    m.name,
    m.gender,
    m.birth_date,
    m.death_date,
    TIMESTAMPDIFF(YEAR, m.birth_date, m.death_date) as lifespan,
    m.generation
FROM Member m
WHERE m.genealogy_id = ?
    AND m.generation = ?
    AND m.birth_date IS NOT NULL
    AND m.death_date IS NOT NULL
ORDER BY m.birth_date
)";

                auto members_result = client->execSqlSync(members_sql, genealogy_id, target_generation);

                Value membersArray(Json::arrayValue);
                int male_count = 0, female_count = 0;
                double total_lifespan = 0.0;

                for (const auto& row : members_result) {
                    Value member;
                    member["member_id"] = row["member_id"].as<int>();
                    member["name"] = row["name"].as<std::string>();
                    member["gender"] = row["gender"].as<std::string>();
                    member["birth_date"] = dbutils::safeStr(row, "birth_date");
                    member["death_date"] = dbutils::safeStr(row, "death_date");
                    member["lifespan"] = row["lifespan"].as<int>();
                    member["generation"] = dbutils::safeInt(row, "generation");
                    membersArray.append(member);

                    if (row["gender"].as<std::string>() == "male") male_count++;
                    else if (row["gender"].as<std::string>() == "female") female_count++;
                    total_lifespan += row["lifespan"].as<int>();
                }

                data["members"] = membersArray;

                Value stats;
                stats["male_count"] = male_count;
                stats["female_count"] = female_count;
                stats["avg_lifespan"] = membersArray.empty() ? 0.0 : total_lifespan / membersArray.size();
                stats["total_members"] = static_cast<int>(membersArray.size());
                data["generation_stats"] = stats;

            } else {
                data["generation"] = 0;
                data["avg_lifespan"] = 0.0;
                data["member_count"] = 0;
                data["members"] = Json::arrayValue;
                data["generation_stats"] = Json::objectValue;
            }

            // Phase 3: 缓存结果（TTL 30 分钟）
            cache::StatsCache::instance().set(cache_key, data, 1800);

            callback(createApiResponse(200, "获取成功", data));

        } catch (const std::exception& e) {
            std::cerr << "统计平均寿命错误: " << e.what() << std::endl;
            callback(createApiResponse(500, "统计失败: " + std::string(e.what())));
        }

    } catch (const std::exception& e) {
        std::cerr << "获取平均寿命异常: " << e.what() << std::endl;
        callback(createApiResponse(500, "服务器内部错误"));
    }
}
