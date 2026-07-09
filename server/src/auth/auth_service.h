// auth_service.h — 用户认证服务，argon2id 密码哈希 + 用户管理
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <optional>
#include <mutex>

/**
 * AuthService 单例 — 管理用户注册、密码哈希、登录验证。
 * 密码使用 libsodium argon2id 哈希（crypto_pwhash_str），每个用户独立 salt。
 * 用户数据持久化到 config/users.json。
 */
class AuthService {
public:
    static AuthService& instance();

    AuthService(const AuthService&) = delete;
    AuthService& operator=(const AuthService&) = delete;

    /** 从 config 目录加载用户文件。必须在使用前调用 */
    bool initialize(const std::string& config_dir);

    /** 创建新用户（密码自动哈希） */
    std::optional<User> createUser(const std::string& username,
                                   const std::string& password,
                                   UserRole role);

    /** 验证用户名密码。返回用户对象，失败返回 nullopt */
    std::optional<User> verifyUser(const std::string& username,
                                   const std::string& password);

    /** 按用户名查找 */
    const User* findUser(const std::string& username) const;

    /** 按 ID 查找 */
    const User* findUserById(const std::string& id) const;

    /** 所有用户列表 */
    const std::vector<User>& getAllUsers() const { return users_; }

private:
    AuthService() = default;

    // ── 内部 ──
    bool loadUsers(const std::string& config_dir);
    bool saveUsers(const std::string& config_dir);
    void createSeedUsers();

    // ── 数据 ──
    std::vector<User> users_;
    std::string config_dir_;
    bool initialized_ = false;
    mutable std::mutex mutex_;
};
