// auth_service.cpp — AuthService 实现
#include "auth/auth_service.h"
#include "core/logger.h"

#include <sodium.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

/** 用 argon2id 哈希密码（libsodium crypto_pwhash_str），自动生成独立 salt */
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

/** 验证密码是否匹配已存储的 argon2id 哈希 */
bool verifyPassword(const std::string& password, const std::string& hash) {
    if (hash.empty()) return false;
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(),
                                    password.size()) == 0;
}

// ── UUID 生成 ──

/** 生成 UUID v4（不依赖外部库） */
std::string generateUuid() {
        static std::mt19937_64 rng(std::random_device{}());
        static std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

        uint64_t a = dist(rng);
        uint64_t b = dist(rng);

        std::ostringstream oss;
        oss << std::hex << std::setfill('0')
            << std::setw(8) << ((a >> 32) & 0xFFFFFFFF)
            << "-" << std::setw(4) << ((a >> 16) & 0xFFFF)
            << "-4" << std::setw(3) << (a & 0x0FFF)  // version 4
            << "-8" << std::setw(3) << ((b >> 48) & 0x0FFF)  // variant 8-b
            << "-" << std::setw(4) << ((b >> 32) & 0xFFFF)
            << std::setw(8) << (b & 0xFFFFFFFF);
        return oss.str();
    }
}

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

    // 初始化 libsodium（幂等，重复调用安全）
    if (sodium_init() < 0) {
        Logger::instance().error("libsodium initialization failed");
        return false;
    }

    if (!loadUsers(config_dir)) {
        // 文件不存在或损坏 → 创建种子用户
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

    // 用户名唯一性校验
    auto it = std::find_if(users_.begin(), users_.end(),
        [&](const User& u) { return u.username == username; });
    if (it != users_.end()) {
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
    saveUsers(config_dir_);

    Logger::instance().info("User created: " + username);
    return user;
}

// ── 登录验证 ──

std::optional<User> AuthService::verifyUser(const std::string& username,
                                            const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(users_.begin(), users_.end(),
        [&](const User& u) { return u.username == username; });

    if (it == users_.end()) {
        return std::nullopt;  // 用户不存在
    }

    // 检查账号状态
    if (!it->active) {
        Logger::instance().warn("Login attempt on inactive account: " + username);
        return std::nullopt;
    }

    // 检查是否锁定
    if (!it->locked_until.empty()) {
        // 简单实现：比较 ISO 8601 字符串（字典序等价于时间序）
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream now_str;
        now_str << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");

        if (it->locked_until > now_str.str()) {
            Logger::instance().warn("Login attempt on locked account: " + username);
            return std::nullopt;
        } else {
            // 锁定已过期，自动解除
            it->locked_until.clear();
            it->failed_attempts = 0;
        }
    }

    // 验证密码
    if (!verifyPassword(password, it->password_hash)) {
        it->failed_attempts++;
        Logger::instance().warn("Failed login for " + username
            + " (" + std::to_string(it->failed_attempts) + "/5)");

        // 5 次失败锁定 30 分钟
        if (it->failed_attempts >= 5) {
            auto now = std::chrono::system_clock::now();
            auto lock_time = now + std::chrono::minutes(30);
            auto t = std::chrono::system_clock::to_time_t(lock_time);
            std::ostringstream lock_str;
            lock_str << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
            it->locked_until = lock_str.str();

            Logger::instance().warn("Account locked for 30 min: " + username);
        }

        saveUsers(config_dir_);
        return std::nullopt;
    }

    // 登录成功，重置失败计数
    it->failed_attempts = 0;
    it->locked_until.clear();
    saveUsers(config_dir_);

    Logger::instance().info("User logged in: " + username);
    return *it;
}

// ── 查询 ──

const User* AuthService::findUser(const std::string& username) const {
    auto it = std::find_if(users_.begin(), users_.end(),
        [&](const User& u) { return u.username == username; });
    return (it != users_.end()) ? &(*it) : nullptr;
}

const User* AuthService::findUserById(const std::string& id) const {
    auto it = std::find_if(users_.begin(), users_.end(),
        [&](const User& u) { return u.id == id; });
    return (it != users_.end()) ? &(*it) : nullptr;
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

    // 三个预置账号，密码与用户名相同（首次登录后建议修改）
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

    Logger::instance().info("Created 3 seed users: admin, staff, passenger");
}
