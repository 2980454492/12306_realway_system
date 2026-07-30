// rate_limiter.cpp — Token Bucket 限流实现
#include "rate_limiter.h"
#include "system/logger.h"

RateLimiter& RateLimiter::instance() {
    static RateLimiter rl;
    return rl;
}

bool RateLimiter::allow(const std::string& key, int max_tokens, double refill_rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        // 首次请求：满桶
        buckets_[key] = {static_cast<double>(max_tokens) - 1, now};
        return true;
    }

    auto& b = it->second;
    // 按时间补充 token
    double elapsed = std::chrono::duration<double>(now - b.last_refill).count();
    b.tokens = std::min(static_cast<double>(max_tokens), b.tokens + elapsed * refill_rate);

    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        b.last_refill = now;
        return true;
    }

    return false;  // 超限
}

void RateLimiter::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = buckets_.begin();
    while (it != buckets_.end()) {
        if (std::chrono::duration<double>(now - it->second.last_refill).count() > 600) {
            it = buckets_.erase(it);
        } else {
            ++it;
        }
    }
}
