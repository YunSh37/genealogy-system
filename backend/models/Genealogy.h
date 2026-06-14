// models/Genealogy.h
#pragma once
#include <string>
#include <json/json.h>

namespace models {
struct Genealogy {
    int genealogy_id = 0;
    std::string name;
    std::string surname;
    std::string founder_name;
    std::string create_time;
    std::string description;
    int user_id = 0;
    std::string role;  // 新增：当前用户在此族谱中的角色（owner, editor, viewer）
    
    // 转换为JSON
    Json::Value toJson() const {
        Json::Value json;
        json["genealogy_id"] = genealogy_id;
        json["name"] = name;
        json["surname"] = surname;
        if (!founder_name.empty()) json["founder_name"] = founder_name;
        if (!create_time.empty()) json["create_time"] = create_time;
        if (!description.empty()) json["description"] = description;
        json["user_id"] = user_id;
        if (!role.empty()) json["role"] = role;  // 返回角色信息
        return json;
    }
};
}