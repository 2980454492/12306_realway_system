// auth_service.h — 用户认证服务，argon2id 密码哈希 + 用户管理
#pragma once

#include "models.h"

#include <string>
#include <vector>
#include <unordered_map>
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

    /** 从 config/users.json 加载用户文件。必须在使用前调用 */
    bool initialize();

    /** 创建新用户，密码自动哈希。返回 nullopt 表示用户名已存在 */
    std::optional<User> createUser(const std::string& username,
                                   const std::string& password,
                                   UserRole role);

    /** 更新用户字段。role/active 传入则更新，password 非空则重置密码。
     *  传入 current_user_id 用于防止越权操作。 */
    struct UpdateResult { bool success = false; std::string error; };
    UpdateResult updateUser(const std::string& target_id,
                           const std::string& current_user_id,
                           std::optional<UserRole> role,
                           std::optional<bool> active,
                           const std::string& new_password);

    /** 删除用户。不可删除最后一个 SYS_ADMIN。
     *  仅 SYS_ADMIN 可删除自己（不可为最后一个）。 */
    struct DeleteResult { bool success = false; std::string error; };
    DeleteResult deleteUser(const std::string& target_id,
                           const std::string& current_user_id);

    /** 验证用户名密码。返回用户对象，失败返回 nullopt */
    std::optional<User> verifyUser(const std::string& username,
                                   const std::string& password);

    /** 按用户名查找 */
    const User* findUser(const std::string& username) const;

    /** 按 ID 查找 */
    const User* findUserById(const std::string& id) const;

    /** 所有用户列表（返回副本，线程安全） */
    std::vector<User> getAllUsers() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return users_;
    }

private:
    AuthService() = default;

    // ── 内部 ──
    bool loadUsers();
    bool saveUsers() const;
    void createSeedUsers();

    /** 重建索引（加载/修改用户后调用） */
    void rebuildIndexes();

    // ── 数据 ──
    std::vector<User> users_;
    std::unordered_map<std::string, size_t> username_idx_;  // username → users_ 下标
    std::unordered_map<std::string, size_t> id_idx_;        // user id → users_ 下标
    bool initialized_ = false;
    mutable std::mutex mutex_;
};
