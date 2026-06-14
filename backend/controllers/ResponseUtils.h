// controllers/ResponseUtils.h
#pragma once
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace myutils {
drogon::HttpResponsePtr createApiResponse(int code, const std::string& message, const Json::Value& data = Json::Value());
}