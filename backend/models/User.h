// models/User.h
#pragma once
#include <string>

namespace models {
struct User {
    int user_id = 0;
    std::string username;
    std::string password;
    std::string created_at;
};
}