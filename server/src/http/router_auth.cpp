// This file is part of routes.cpp — auto-split by module
#include "router_helpers.h"

void registerAuthRoutes(RailwayServer& server) {
    auto& app = server.getApp();
    // ── GET /health — 健康检查 ──
    // 返回服务状态、运行时长、版本号
    auto start_time = std::chrono::steady_clock::now();  // 服务启动时间

    app.Get("/health", [start_time](const httplib::Request& /*req*/, httplib::Response& res) {
    try {
        auto now = std::chrono::steady_clock::now();
        auto uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        json j;
        j["ok"] = true;
        j["uptime"] = uptime_sec;
        j["version"] = "0.1.0";

        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("/health error: ") + e.what());
        json j;
        j["ok"] = false;
        j["error"] = "Internal server error";
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── POST /api/auth/login — 登录 ──
    app.Post("/api/auth/login", [](const httplib::Request& req, httplib::Response& res) {
    if (!checkRateLimit("login:" + clientIP(req), 10, 10.0/60, res)) return;
    try {
        json body = json::parse(req.body);
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");

        if (username.empty() || password.empty()) {
            json j;
            j["ok"] = false;
            j["error"] = "username and password are required";
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }

        auto user = AuthService::instance().verifyUser(username, password);
        if (!user) {
            AuditLogger::instance().log("", "", "LOGIN",
                "user:" + username, "", "failure");
            json j;
            j["ok"] = false;
            j["error"] = "Invalid credentials or account locked";
            res.set_content(j.dump(), "application/json");
            res.status = 401;
            return;
        }

        // 生成 JWT token（30 分钟有效期）
        std::string role_str;
        switch (user->role) {
            case UserRole::SYS_ADMIN:   role_str = "SYS_ADMIN"; break;
            case UserRole::INFRA_ADMIN: role_str = "INFRA_ADMIN"; break;
            case UserRole::STAFF:       role_str = "STAFF"; break;
            case UserRole::APPROVER:    role_str = "APPROVER"; break;
            case UserRole::PASSENGER:   role_str = "PASSENGER"; break;
        }
        std::string token = JwtService::instance().generateToken(
            user->id, role_str, 1800);
        AuditLogger::instance().log(user->id, role_str, "LOGIN",
            "user:" + user->username, "", "success");

        json j;
        j["ok"] = true;
        j["token"] = token;
        j["token_type"] = "Bearer";
        j["expires_in"] = 1800;
        j["user_id"] = user->id;
        j["username"] = user->username;
        j["role"] = role_str;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = std::string("Login error: ") + e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── POST /api/auth/register — 旅客自助注册 ──
    app.Post("/api/auth/register", [](const httplib::Request& req, httplib::Response& res) {
    if (!checkRateLimit("register:" + clientIP(req), 5, 5.0/3600, res)) return;
    try {
        json body = json::parse(req.body);
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");

        if (username.empty() || password.empty()) {
            json j;
            j["ok"] = false;
            j["error"] = "用户名和密码不能为空";
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }
        if (username.length() < 3 || password.length() < 6) {
            json j;
            j["ok"] = false;
            j["error"] = "用户名至少3位，密码至少6位";
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }

        auto& auth = AuthService::instance();
        auto user = auth.createUser(username, password, UserRole::PASSENGER);
        if (!user) {
            json j;
            j["ok"] = false;
            j["error"] = "用户名已存在";
            res.set_content(j.dump(), "application/json");
            res.status = 409;
            return;
        }

        // 注册成功自动登录，返回 JWT
        std::string token = JwtService::instance().generateToken(
            user->id, "PASSENGER", 1800);

        json j;
        j["ok"] = true;
        j["token"] = token;
        j["token_type"] = "Bearer";
        j["expires_in"] = 1800;
        j["user_id"] = user->id;
        j["username"] = user->username;
        j["role"] = "PASSENGER";
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = std::string("注册失败: ") + e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── GET /api/whoami — 验证 JWT + RBAC 中间件（调试验证用）──
    app.Get("/api/whoami", [](const httplib::Request& req, httplib::Response& res) {
    try {
        // 1. 鉴权：从 Authorization header 提取并校验 JWT
        std::string auth = req.has_header("Authorization")
            ? req.get_header_value("Authorization") : "";

        auto ctx = RbacMiddleware::authenticate(auth);
        if (!ctx) {
            json j;
            j["ok"] = false;
            j["error"] = "Unauthorized: invalid or expired token";
            res.set_content(j.dump(), "application/json");
            res.status = 401;
            return;
        }

        // 2. 鉴权通过，返回用户信息
        json j;
        j["ok"] = true;
        j["user_id"] = ctx->user_id;
        j["role"] = ctx->role;
        j["permissions"] = ctx->permissions.to_ullong();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

}
