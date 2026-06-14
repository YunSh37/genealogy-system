// controllers/ResponseUtils.cc
#include "ResponseUtils.h"
#include <drogon/drogon.h>

using namespace drogon;
using Json::Value;

drogon::HttpResponsePtr myutils::createApiResponse(int code, const std::string& message, const Json::Value& data) {
    Value json;
    json["code"] = code;
    json["message"] = message;
    if (!data.isNull()) {
        json["data"] = data;
    }
    
    auto resp = HttpResponse::newHttpJsonResponse(json);
    
    // 状态码映射
    switch(code) {
        case 200: resp->setStatusCode(k200OK); break;
        case 201: resp->setStatusCode(k201Created); break;
        case 202: resp->setStatusCode(k202Accepted); break;
        case 204: resp->setStatusCode(k204NoContent); break;
        case 400: resp->setStatusCode(k400BadRequest); break;
        case 401: resp->setStatusCode(k401Unauthorized); break;
        case 403: resp->setStatusCode(k403Forbidden); break;
        case 404: resp->setStatusCode(k404NotFound); break;
        case 409: resp->setStatusCode(k409Conflict); break;
        case 422: resp->setStatusCode(k422UnprocessableEntity); break;
        case 500: resp->setStatusCode(k500InternalServerError); break;
        case 502: resp->setStatusCode(k502BadGateway); break;
        case 503: resp->setStatusCode(k503ServiceUnavailable); break;
        default: resp->setStatusCode(k500InternalServerError); break;
    }
    
    return resp;
}