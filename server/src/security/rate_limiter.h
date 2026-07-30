// rate_limiter.h — Token Bucket 限流器，防刷票/暴力登录
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

/**
 * RateLimiter 单例 — 基于 Token Bucket 算法的请求限流。
 * 每个 key（IP 或 IP+endpoint）独立计数，超过速率限制返回 false。
 *
 * 用法：if (!RateLimiter::instance().allow("login:" + ip, 5, 5.0/60)) { 429 }
 *       max_tokens=5: 突发最多 5 个请求
 *       refill_rate=5.0/60: 每 60 秒补充 5 个 token，即 5 次/分钟
 */
class RateLimiter {
public:
    static RateLimiter& instance();

    RateLimiter(const RateLimiter&) = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;

    /**
     * 检查请求是否允许。
     * @param key         限流键（如 "login:127.0.0.1"）
     * @param max_tokens  桶容量（突发上限）
     * @param refill_rate 补充速率（token/秒）
     * @return true 允许，false 超限
     */
    bool allow(const std::string& key, int max_tokens, double refill_rate);

    /** 清理过期桶（超过 10 分钟未使用的 key 删除） */
    void cleanup();

private:
    RateLimiter() = default;

    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    std::unordered_map<std::string, Bucket> buckets_;
    std::mutex mutex_;
};
