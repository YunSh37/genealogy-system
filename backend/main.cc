// main.cc
#include <drogon/drogon.h>
#include <iostream>


int main() {
    // 设置日志级别
    drogon::app().setLogLevel(trantor::Logger::kDebug);
    
    // 加载配置文件
    try {
        drogon::app().loadConfigFile("../config.json");
    } catch (const std::exception& e) {
        std::cerr << "配置文件错误: " << e.what() << std::endl;
        return 1;
    }
    
    // 启动前打印信息
    drogon::app().registerBeginningAdvice([](){
        std::cout << "=========================================" << std::endl;
        std::cout << "家谱系统服务器启动成功!" << std::endl;
        std::cout << "版本: 1.0.0" << std::endl;
        std::cout << "访问地址: http://localhost:8088" << std::endl;
        std::cout << "=========================================" << std::endl;
    });
    
    // 根路径
    drogon::app().registerHandler("/",
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            Json::Value json;
            json["service"] = "Genealogy System API";
            json["version"] = "1.0.0";
            json["status"] = "running";
            json["timestamp"] = time(nullptr);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            callback(resp);
        });

    // 健康检查接口（轻量，不查数据库）
    drogon::app().registerHandler("/api/health",
        [](const drogon::HttpRequestPtr &req,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            Json::Value json;
            json["code"] = 200;
            json["message"] = "ok";
            json["data"]["status"] = "healthy";
            json["data"]["timestamp"] = static_cast<Json::Int64>(time(nullptr));

            auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::k200OK);
            callback(resp);
        });
    
    // 运行服务器
    try {
        
        drogon::app().run();
    } catch (const std::exception& e) {
        std::cerr << "服务器启动失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}