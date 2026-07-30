// auth_service.cpp — AuthService 实现
#include "auth/auth_service.h"
#include "config.h"
#include "system/logger.h"
#include "utils.h"
#include "system/wal.h"
#include "sys_admin/audit_service.h"

#include <sodium.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// 用 argon2id 哈希密码，自动生成独立 salt
std::string hashPassword(const std::string& password) {
    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hash, password.c_str(), password.size(),
                          crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        Logger::instance().error("Password hashing failed (out of memory?)");
        return "";
    }
    return std::string(hash);
}

// 验证密码是否匹配已存储的 argon2id 哈希
bool verifyPassword(const std::string& password, const std::string& hash) {
    if (hash.empty()) return false;
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(),
                                    password.size()) == 0;
}

}  // namespace

// ── 单例 ──

AuthService& AuthService::instance() {
    static AuthService auth;
    return auth;
}

// ── 初始化 ──

bool AuthService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) return true;

    if (sodium_init() < 0) {
        Logger::instance().error("libsodium initialization failed");
        return false;
    }

    if (!loadUsers()) {
        Logger::instance().info("users.json not found, creating seed users...");
        createSeedUsers();
        saveUsers();
    }

    initialized_ = true;
    Logger::instance().info("AuthService ready: " + std::to_string(users_.size()) + " users");
    return true;
}

// ── 用户管理 ──

std::optional<User> AuthService::createUser(const std::string& username,
                                            const std::string& password,
                                            UserRole role) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (username_idx_.count(username)) {
        Logger::instance().warn("Duplicate username: " + username);
        return std::nullopt;
    }

    User user;
    user.id = generateUuid();
    user.username = username;
    user.password_hash = hashPassword(password);
    user.role = role;
    user.active = true;
    user.failed_attempts = 0;

    users_.push_back(user);
    rebuildIndexes();
    saveUsers();
    WalWriter::instance().append("USER_CREATE", json(user).dump());
    AuditLogger::instance().log(user.id, roleToString(user.role), "USER_CREATE",
        "user:" + username, json({{"role", roleToString(user.role)}}).dump(), "success");

    Logger::instance().info("User created: " + username);
    return user;
}

// ── 用户更新 ──

AuthService::UpdateResult AuthService::updateUser(const std::string& target_id,
                                                  const std::string& current_user_id,
                                                  std::optional<UserRole> role,
                                                  std::optional<bool> active,
                                                  const std::string& new_password) {
    UpdateResult result;
    (void)current_user_id;  // 预留给后续权限校验（如：仅SYS_ADMIN可修改角色）
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = id_idx_.find(target_id);
    if (it == id_idx_.end()) {
        result.error = "用户不存在";
        return result;
    }
    auto& u = users_[it->second];

    if (role)
        u.role = *role;
    if (active) {
        // 不可禁用最后一个 SYS_ADMIN
        if (!*active && u.role == UserRole::SYS_ADMIN) {
            int sys_admin_count = 0;
            for (const auto& user : users_)
                if (user.role == UserRole::SYS_ADMIN && user.active) ++sys_admin_count;
            if (sys_admin_count <= 1) {
                result.error = "不可禁用最后一个系统管理员";
                return result;
            }
        }
        u.active = *active;
    }
    if (!new_password.empty())
        u.password_hash = hashPassword(new_password);

    saveUsers();
    WalWriter::instance().append("USER_UPDATE", json(u).dump());
    AuditLogger::instance().log(current_user_id, "", "USER_UPDATE",
        "user:" + u.username, json({{"role", roleToString(u.role)}, {"active", u.active}}).dump(), "success");
    result.success = true;
    Logger::instance().info("User updated: " + u.username);
    return result;
}

// ── 用户删除 ──

AuthService::DeleteResult AuthService::deleteUser(const std::string& target_id,
                                                  const std::string& current_user_id) {
    DeleteResult result;
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = id_idx_.find(target_id);
    if (it == id_idx_.end()) {
        result.error = "用户不存在";
        return result;
    }
    auto& u = users_[it->second];

    // 1. 检查是否为最后一个活跃 SYS_ADMIN
    if (u.role == UserRole::SYS_ADMIN) {
        int sys_admin_count = 0;
        for (const auto& user : users_)
            if (user.role == UserRole::SYS_ADMIN && user.active) ++sys_admin_count;
        if (sys_admin_count <= 1) {
            result.error = "不可删除最后一个系统管理员";
            return result;
        }
    }

    // 2. 非 SYS_ADMIN 不可删除自己
    if (target_id == current_user_id) {
        auto* self = findUserById(current_user_id);
        if (!self || self->role != UserRole::SYS_ADMIN) {
            result.error = "仅系统管理员可删除自己";
            return result;
        }
    }

    // 3. 执行删除（交换到末尾后弹出，保持索引有效）
    size_t idx = it->second;
    size_t last = users_.size() - 1;
    if (idx != last) {
        std::swap(users_[idx], users_[last]);
        id_idx_[users_[idx].id] = idx;
    }
    users_.pop_back();
    id_idx_.erase(target_id);
    username_idx_.erase(u.username);

    saveUsers();
    WalWriter::instance().append("USER_DELETE", json({{"id", target_id}}).dump());
    AuditLogger::instance().log(current_user_id, "", "USER_DELETE",
        "user:" + u.username, json({{"target_id", target_id}}).dump(), "success");
    result.success = true;
    Logger::instance().info("User deleted: " + u.username);
    return result;
}

// ── 登录验证 ──

std::optional<User> AuthService::verifyUser(const std::string& username,
                                            const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = username_idx_.find(username);
    if (it == username_idx_.end()) {
        return std::nullopt;
    }
    auto& u = users_[it->second];

    if (!u.active) {
        Logger::instance().warn("Login attempt on inactive account: " + username);
        return std::nullopt;
    }

    if (!u.locked_until.empty()) {
        if (u.locked_until > nowIso()) {
            Logger::instance().warn("Login attempt on locked account: " + username);
            return std::nullopt;
        } else {
            u.locked_until.clear();
            u.failed_attempts = 0;
        }
    }

    if (!verifyPassword(password, u.password_hash)) {
        u.failed_attempts++;
        Logger::instance().warn("Failed login for " + username
            + " (" + std::to_string(u.failed_attempts) + "/5)");

        if (u.failed_attempts >= 5) {
            auto now = std::chrono::system_clock::now();
            auto lock_time = now + std::chrono::minutes(30);
            auto t = std::chrono::system_clock::to_time_t(lock_time);
            std::ostringstream lock_str;
            lock_str << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
            u.locked_until = lock_str.str();
            Logger::instance().warn("Account locked for 30 min: " + username);
        }

        saveUsers();
        return std::nullopt;
    }

    u.failed_attempts = 0;
    u.locked_until.clear();
    saveUsers();

    Logger::instance().info("User logged in: " + username);
    return u;
}

// ── 查询 ──

const User* AuthService::findUser(const std::string& username) const {
    auto it = username_idx_.find(username);
    return (it != username_idx_.end()) ? &users_[it->second] : nullptr;
}

const User* AuthService::findUserById(const std::string& id) const {
    auto it = id_idx_.find(id);
    return (it != id_idx_.end()) ? &users_[it->second] : nullptr;
}

// ── 索引维护 ──

void AuthService::rebuildIndexes() {
    username_idx_.clear();
    id_idx_.clear();
    for (size_t i = 0; i < users_.size(); ++i) {
        username_idx_[users_[i].username] = i;
        id_idx_[users_[i].id] = i;
    }
}

// ── 持久化 ──

bool AuthService::loadUsers() {
    std::string path = config::USERS_FILE;
    if (!fs::exists(path)) return false;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;
        users_ = j.get<std::vector<User>>();
        rebuildIndexes();
        Logger::instance().info("Loaded " + std::to_string(users_.size()) + " users");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to parse users.json: ") + e.what());
        return false;
    }
}

bool AuthService::saveUsers() const {
    std::string path = config::USERS_FILE;
    try {
        json j = users_;
        std::ofstream out(path);
        out << j.dump(2);
        out.close();
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Failed to save users.json: ") + e.what());
        return false;
    }
}

// ── 种子用户 ──

void AuthService::createSeedUsers() {
    users_.clear();

    User infra_admin;
    infra_admin.id = generateUuid();
    infra_admin.username = "infra_admin";
    infra_admin.password_hash = hashPassword("infra123");
    infra_admin.role = UserRole::INFRA_ADMIN;
    users_.push_back(infra_admin);

    User sys_admin;
    sys_admin.id = generateUuid();
    sys_admin.username = "sys_admin";
    sys_admin.password_hash = hashPassword("sys123");
    sys_admin.role = UserRole::SYS_ADMIN;
    users_.push_back(sys_admin);

    User staff;
    staff.id = generateUuid();
    staff.username = "staff";
    staff.password_hash = hashPassword("staff123");
    staff.role = UserRole::STAFF;
    users_.push_back(staff);

    User approver;
    approver.id = generateUuid();
    approver.username = "approver";
    approver.password_hash = hashPassword("approver123");
    approver.role = UserRole::APPROVER;
    users_.push_back(approver);

    User passenger;
    passenger.id = generateUuid();
    passenger.username = "passenger";
    passenger.password_hash = hashPassword("pass123");
    passenger.role = UserRole::PASSENGER;
    users_.push_back(passenger);

    rebuildIndexes();
    Logger::instance().info("Created 5 seed users: infra_admin, sys_admin, staff, approver, passenger");
}
