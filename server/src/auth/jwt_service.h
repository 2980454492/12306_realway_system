// jwt_service.h — JWT 令牌服务，HS256 签名（libsodium HMAC-SHA256）
#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <optional>

/**
 * JwtService — JWT 令牌的生成与校验。
 * 使用 HS256（HMAC-SHA256），密钥为启动时随机生成或配置文件指定。
 * Payload 字段：sub(userId)、role、iat、exp。
 */
class JwtService {
public:
    static JwtService& instance();

    JwtService(const JwtService&) = delete;
    JwtService& operator=(const JwtService&) = delete;

    /** 初始化：设置签名密钥。密钥为空则随机生成（重启后所有旧 Token 失效） */
    void initialize(const std::string& secret_key = "");

    /** 生成 JWT token。expires_in_seconds 默认 30 分钟 */
    std::string generateToken(const std::string& user_id,
                              const std::string& role,
                              int expires_in_seconds = 1800) const;

    /** JWT 解析后的 Payload 字段 */
    struct Payload {
        std::string user_id;
        std::string role;
        int64_t exp = 0;
        int64_t iat = 0;
    };

    /** 校验 JWT token。成功返回 payload，失败返回 nullopt */
    std::optional<Payload> verifyToken(const std::string& token) const;

    /** 获取密钥（用于调试，生产环境勿暴露） */
    const std::string& getSecretKey() const { return secret_key_; }

private:
    JwtService() = default;

    std::string secret_key_;
    bool initialized_ = false;
};
