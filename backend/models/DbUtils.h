// models/DbUtils.h — 安全访问数据库行字段（防止 NULL 导致崩溃）
#pragma once
#include <drogon/orm/DbClient.h>
#include <string>

namespace dbutils {

// 安全读取可能为 NULL 的字符串列（birth_date, death_date, biography 等）
inline std::string safeStr(const drogon::orm::Row& row, const std::string& key) {
    try {
        auto field = row[key];
        if (!field.isNull()) {
            return field.as<std::string>();
        }
    } catch (...) {}
    return "";
}

// 安全读取可能为 NULL 的整数列（father_id, mother_id, spouse_id, generation 等）
inline int safeInt(const drogon::orm::Row& row, const std::string& key, int def = 0) {
    try {
        auto field = row[key];
        if (!field.isNull()) {
            return field.as<int>();
        }
    } catch (...) {}
    return def;
}

} // namespace dbutils
