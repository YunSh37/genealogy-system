// models/Marriage.h
#pragma once
#include <string>
#include <json/json.h>

namespace models {
struct Marriage {
    int marriage_id = 0;
    int husband_id = 0;
    int wife_id = 0;
    std::string wedding_date;
    std::string status;  // "active" 或 "dissolved"
    std::string created_at;
    
    // 转换为JSON
    Json::Value toJson() const {
        Json::Value json;
        json["marriage_id"] = marriage_id;
        json["husband_id"] = husband_id;
        json["wife_id"] = wife_id;
        if (!wedding_date.empty()) json["wedding_date"] = wedding_date;
        json["status"] = status;
        if (!created_at.empty()) json["created_at"] = created_at;
        return json;
    }
};
}