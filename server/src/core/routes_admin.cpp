// Auto-split from routes.cpp
#include "core/routes_helpers.h"

void registerAdminRoutes(RailwayServer& server) {
    auto& app = server.getApp();
    // ── GET /api/admin/debug — 管理员权限测试（仅 ADMIN 可访问）──
    app.Get("/api/admin/debug", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_USERS);
        if (!ctx) return;

        json j;
        j["ok"] = true;
        j["message"] = "Welcome, admin " + ctx->user_id;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── 用户管理（SYS_ADMIN）──

    // GET /api/admin/users — 用户列表（脱敏密码哈希）
    app.Get("/api/admin/users", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_USERS);
        if (!ctx) return;

        json arr = json::array();
        for (const auto& u : AuthService::instance().getAllUsers()) {
            json ju = u;
            ju.erase("password_hash");
            arr.push_back(ju);
        }
        json j;
        j["ok"] = true;
        j["data"] = arr;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // POST /api/admin/users — 创建用户（SYS_ADMIN 可建任意角色）
    app.Post("/api/admin/users", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_USERS);
        if (!ctx) return;

        json body = json::parse(req.body);
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");
        std::string role_str = body.value("role", "PASSENGER");

        if (username.empty() || password.empty()) {
            json j;
            j["ok"] = false;
            j["error"] = "用户名和密码不能为空";
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }

        UserRole role = roleFromString(role_str);

        auto user = AuthService::instance().createUser(username, password, role);
        if (!user) {
            json j;
            j["ok"] = false;
            j["error"] = "用户名已存在";
            res.set_content(j.dump(), "application/json");
            res.status = 409;
            return;
        }

        json j;
        j["ok"] = true;
        json ju = *user;
        ju.erase("password_hash");
        j["user"] = ju;
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // PUT /api/admin/users/{id} — 更新用户（角色/状态/密码）
    app.Put(R"(/api/admin/users/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_USERS);
        if (!ctx) return;

        std::string target_id = req.matches[1];
        json body = json::parse(req.body);

        std::optional<UserRole> role;
        if (body.contains("role"))
            role = roleFromString(body["role"]);

        std::optional<bool> active;
        if (body.contains("active"))
            active = body["active"].get<bool>();

        std::string new_password = body.value("password", "");

        auto result = AuthService::instance().updateUser(
            target_id, ctx->user_id, role, active, new_password);
        if (!result.success) {
            json j;
            j["ok"] = false;
            j["error"] = result.error;
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }

        json j;
        j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // DELETE /api/admin/users/{id} — 删除用户
    app.Delete(R"(/api/admin/users/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_USERS);
        if (!ctx) return;

        std::string target_id = req.matches[1];
        auto result = AuthService::instance().deleteUser(target_id, ctx->user_id);
        if (!result.success) {
            json j;
            j["ok"] = false;
            j["error"] = result.error;
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }

        json j;
        j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── GET /api/admin/audit — 审计日志查询（SYS_ADMIN）──
    app.Get("/api/admin/audit", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::VIEW_AUDIT);
        if (!ctx) return;

        auto records = AuditLogger::instance().getRecords();
        // 筛选
        std::string from = req.get_param_value("from");
        std::string to = req.get_param_value("to");
        std::string user = req.get_param_value("user");
        std::string action = req.get_param_value("action");
        int limit = 200;
        try { if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit")); }
        catch (...) {}

        json arr = json::array();
        int count = 0;
        // 倒序遍历（最新的在前）
        for (int i = static_cast<int>(records.size()) - 1; i >= 0 && count < limit; --i) {
            const auto& r = records[i];
            if (!from.empty() && r.timestamp < from) continue;
            if (!to.empty() && r.timestamp > to) continue;
            if (!user.empty() && r.user_id != user && r.user_id.find(user) == std::string::npos) continue;
            if (!action.empty() && r.action != action) continue;

            json jr = r;
            arr.push_back(jr);
            ++count;
        }

        json j;
        j["ok"] = true;
        j["total"] = records.size();
        j["verified"] = AuditLogger::instance().verifyChain();
        j["data"] = arr;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 500;
    }
    });

    // ── 站点管理（INFRA_ADMIN）──

    app.Get("/api/admin/stations", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        json j;
        j["ok"] = true;
        j["data"] = DataStore::instance().getAllStations();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    app.Post("/api/admin/stations", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        json body = json::parse(req.body);
        Station station = body.get<Station>();
        auto s = DataStore::instance().addStation(station);
        json j;
        j["ok"] = true;
        j["data"] = s;
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    app.Put(R"(/api/admin/stations/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        uint32_t id = std::stoul(req.matches[1]);
        json body = json::parse(req.body);
        Station station = body.get<Station>();
        if (!DataStore::instance().updateStation(id, station)) {
            json j; j["ok"] = false; j["error"] = "站点不存在";
            res.set_content(j.dump(), "application/json"); res.status = 404;
            return;
        }
        json j; j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    app.Delete(R"(/api/admin/stations/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        uint32_t id = std::stoul(req.matches[1]);
        if (!DataStore::instance().removeStation(id)) {
            json j; j["ok"] = false; j["error"] = "站点不存在";
            res.set_content(j.dump(), "application/json"); res.status = 404;
            return;
        }
        json j; j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    // ── 线路管理（INFRA_ADMIN）──

    app.Get("/api/admin/lines", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        json j;
        j["ok"] = true;
        j["data"] = DataStore::instance().getAllLines();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    app.Post("/api/admin/lines", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        json body = json::parse(req.body);
        Line line = body.get<Line>();
        auto l = DataStore::instance().addLine(line);
        json j;
        j["ok"] = true;
        j["data"] = l;
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    app.Put(R"(/api/admin/lines/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        uint32_t id = std::stoul(req.matches[1]);
        json body = json::parse(req.body);
        Line line = body.get<Line>();
        if (!DataStore::instance().updateLine(id, line)) {
            json j; j["ok"] = false; j["error"] = "线路不存在";
            res.set_content(j.dump(), "application/json"); res.status = 404;
            return;
        }
        json j; j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    app.Delete(R"(/api/admin/lines/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        uint32_t id = std::stoul(req.matches[1]);
        if (!DataStore::instance().removeLine(id)) {
            json j; j["ok"] = false; j["error"] = "线路不存在";
            res.set_content(j.dump(), "application/json"); res.status = 404;
            return;
        }
        json j; j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    // ── 查看路网（INFRA_ADMIN）──

}
