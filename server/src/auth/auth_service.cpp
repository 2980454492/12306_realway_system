// auth_service.cpp — AuthService 实现
#include "auth/auth_service.h"
#include "core/logger.h"
#include "core/utils.h"

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

bool AuthService::initialize(const std::string& config_dir) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) return true;

    config_dir_ = config_dir;

    if (sodium_init() < 0) {
        Logger::instance().error("libsodium initialization failed");
        return false;
    }

    if (!loadUsers(config_dir)) {
        Logger::instance().info("users.json not found, creating seed users...");
        createSeedUsers();
        saveUsers(config_dir);
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
    saveUsers(config_dir_);

    Logger::instance().info("User created: " + username);
    return user;
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
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream now_str;
        now_str << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");

        if (u.locked_until > now_str.str()) {
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

        saveUsers(config_dir_);
        return std::nullopt;
    }

    u.failed_attempts = 0;
    u.locked_until.clear();
    saveUsers(config_dir_);

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

bool AuthService::loadUsers(const std::string& config_dir) {
    std::string path = config_dir + "/users.json";
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

bool AuthService::saveUsers(const std::string& config_dir) {
    std::string path = config_dir + "/users.json";
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

    User admin;
    admin.id = generateUuid();
    admin.username = "admin";
    admin.password_hash = hashPassword("admin123");
    admin.role = UserRole::ADMIN;
    users_.push_back(admin);

    User staff;
    staff.id = generateUuid();
    staff.username = "staff";
    staff.password_hash = hashPassword("staff123");
    staff.role = UserRole::STAFF;
    users_.push_back(staff);

    User passenger;
    passenger.id = generateUuid();
    passenger.username = "passenger";
    passenger.password_hash = hashPassword("pass123");
    passenger.role = UserRole::PASSENGER;
    users_.push_back(passenger);

    User approver;
    approver.id = generateUuid();
    approver.username = "approver";
    approver.password_hash = hashPassword("approver123");
    approver.role = UserRole::APPROVER;
    users_.push_back(approver);

    rebuildIndexes();
    Logger::instance().info("Created 4 seed users: admin, staff, approver, passenger");
}
