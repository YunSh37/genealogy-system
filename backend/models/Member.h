// models/Member.h
#pragma once
#include <string>
#include <json/json.h>

namespace models {
struct Member {
    int member_id = 0;
    int genealogy_id = 0;
    std::string name;
    std::string gender;  // "male" 或 "female"
    std::string birth_date;
    std::string death_date;
    std::string biography;
    int father_id = 0;
    int mother_id = 0;
    int spouse_id = 0;
    int generation = 0;  // 辈分：1=始祖，逐代递增

    // 转换为JSON
    Json::Value toJson() const {
        Json::Value json;
        json["member_id"] = member_id;
        json["genealogy_id"] = genealogy_id;
        json["name"] = name;
        json["gender"] = gender;
        json["generation"] = generation;         // 始终输出，前端需要显示辈分列
        json["father_id"] = father_id;           // 始终输出（0=无），前端需要构建树结构
        json["mother_id"] = mother_id;           // 始终输出
        json["spouse_id"] = spouse_id;           // 始终输出
        if (!birth_date.empty()) json["birth_date"] = birth_date;
        if (!death_date.empty()) json["death_date"] = death_date;
        if (!biography.empty()) json["biography"] = biography;
        return json;
    }
};
}
