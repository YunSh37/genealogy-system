// models/StatsCache.h — 内存统计缓存（Phase 3: 替代 Redis 的轻量方案）
// 线程安全的单例，带 TTL 过期机制
#pragma once
#include <json/json.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace cache {

class StatsCache {
public:
    static StatsCache& instance() {
        static StatsCache inst;
        return inst;
    }

    // 获取缓存值（返回 true 如果命中且未过期）
    bool get(const std::string& key, Json::Value& out) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = std::chrono::steady_clock::now();
        auto it = cache_.find(key);
        if (it != cache_.end() && now < it->second.expires_at) {
            out = it->second.data;
            return true;
        }
        // 清理过期条目（惰性删除）
        if (it != cache_.end()) {
            cache_.erase(it);
        }
        return false;
    }

    // 设置缓存值（ttl_seconds 秒后过期）
    void set(const std::string& key, const Json::Value& data, int ttl_seconds = 1800) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto expires = std::chrono::steady_clock::now() + std::chrono::seconds(ttl_seconds);
        cache_[key] = Entry{data, expires};
    }

    // 按前缀批量失效（如 "genealogy_1_" 会使所有以该前缀开头的 key 失效）
    void invalidatePrefix(const std::string& prefix) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first.find(prefix) == 0) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 清理所有过期条目
    void cleanup() {
        std::lock_guard<std::mutex> lock(mtx_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now >= it->second.expires_at) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    StatsCache() = default;
    StatsCache(const StatsCache&) = delete;
    StatsCache& operator=(const StatsCache&) = delete;

    struct Entry {
        Json::Value data;
        std::chrono::steady_clock::time_point expires_at;
    };

    std::mutex mtx_;
    std::unordered_map<std::string, Entry> cache_;
};

} // namespace cache
