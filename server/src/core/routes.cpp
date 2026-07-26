// routes.cpp — 路由注册实现
#include "core/routes.h"
#include "core/config.h"
#include "core/server.h"
#include "core/logger.h"
#include "data/data_store.h"
#include "auth/auth_service.h"
#include "auth/jwt_service.h"
#include "auth/rbac_middleware.h"
#include "passenger/train_query.h"
#include "passenger/order_service.h"
#include "staff/train_manager.h"
#include "staff/approval_service.h"
#include "core/utils.h"
#include "core/wal_writer.h"
#include "core/audit_logger.h"
#include "core/system_config.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <sstream>

using json = nlohmann::json;  // 局部 using，非全局

/** 停站序列 → JSON 数组 [{station_id, station_name, line_id, arrival, departure}] */
inline json stopsToJson(const std::vector<Stop>& stops, DataStore& ds) {
    json arr = json::array();
    for (const auto& stop : stops) {
        json sd;
        sd["station_id"] = stop.station_id;
        sd["station_name"] = stop.station_name.empty()
            ? (ds.getStation(stop.station_id) ? ds.getStation(stop.station_id)->name : "?")
            : stop.station_name;
        sd["line_id"] = stop.line_id;
        sd["line_name"] = stop.line_name;
        sd["arrival"] = stop.arrival;
        sd["departure"] = stop.departure;
        sd["stop_type"] = stop.stop_type;
        arr.push_back(sd);
    }
    return arr;
}

/** 从请求中提取 JWT、校验、检查权限。成功返回 AuthContext，失败写入响应并返回 nullopt。 */
inline std::optional<AuthContext> checkAuth(const httplib::Request& req, httplib::Response& res,
                                             Permission perm) {
    std::string auth = req.has_header("Authorization")
        ? req.get_header_value("Authorization") : "";
    auto ctx = RbacMiddleware::authenticate(auth);
    if (!ctx || !RbacMiddleware::authorize(*ctx, perm)) {
        json j;
        j["ok"] = false;
        j["error"] = ctx ? "Forbidden" : "Unauthorized";
        res.set_content(j.dump(), "application/json");
        res.status = ctx ? 403 : 401;
        return std::nullopt;
    }
    return ctx;
}

void registerRoutes(RailwayServer& server) {
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

    // ── 静态文件托管 — FRONTEND_DIR 目录 ──
    // 所有非 API 路径由前端 SPA 处理
    // index.html 作为兜底页面，SPA 路由由 JS 的 hash-based router 接管
    app.set_mount_point(config::STATIC_MOUNT_POINT, config::FRONTEND_DIR);

    // ── GET /api/debug/stations — 查看所有站点（调试验证用）──
    app.Get("/api/debug/stations", [](const httplib::Request& /*req*/, httplib::Response& res) {
        try {
            auto& ds = DataStore::instance();
            if (!ds.isReady()) {
                json j;
                j["ok"] = false;
                j["error"] = "DataStore not ready";
                res.set_content(j.dump(), "application/json");
                res.status = 503;
                return;
            }

            json j;
            j["ok"] = true;
            j["count"] = ds.getAllStations().size();
            j["data"] = ds.getAllStations();
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── POST /api/auth/login — 登录 ──
    app.Post("/api/auth/login", [](const httplib::Request& req, httplib::Response& res) {
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

    // ── GET/PUT /api/admin/config — 系统配置（SYS_ADMIN）──
    app.Get("/api/admin/config", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::SYSTEM_CONFIG);
            if (!ctx) return;

            json j;
            j["ok"] = true;
            j["data"] = json::parse(SystemConfig::instance().toJson());
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    app.Put("/api/admin/config", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::SYSTEM_CONFIG);
            if (!ctx) return;

            json body = json::parse(req.body);
            auto& cfg = SystemConfig::instance();

            // 费率矩阵：{"G":{"BUSINESS":1.20,...},"D":{...},...}
            if (body.contains("rates")) {
                for (const auto& [prefix_key, seat_obj] : body["rates"].items()) {
                    if (prefix_key.size() != 1) continue;
                    char prefix = prefix_key[0];
                    for (const auto& [seat_key, rate_val] : seat_obj.items()) {
                        SeatType st = nlohmann::json(seat_key).get<SeatType>();
                        cfg.setRate(prefix, st, rate_val.get<double>());
                    }
                }
            }
            if (body.contains("refund_rate_24h") && body.contains("refund_rate_2_24h")
                && body.contains("refund_rate_2h")) {
                cfg.setRefundRates(
                    body["refund_rate_24h"].get<double>(),
                    body["refund_rate_2_24h"].get<double>(),
                    body["refund_rate_2h"].get<double>());
            }

            AuditLogger::instance().log(ctx->user_id, ctx->role, "CONFIG_UPDATE",
                "system", body.dump(), "success");

            json j;
            j["ok"] = true;
            j["data"] = json::parse(cfg.toJson());
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── GET /api/stations/neighbors — 车站-线路-邻居索引（职工新增列车选线用）──
    app.Get("/api/stations/neighbors", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
            if (!ctx) return;

            auto& idx = DataStore::instance().getStationLineIndex();
            json j;
            j["ok"] = true;
            json data = json::object();
            for (const auto& [sid, neighbors] : idx) {
                data[std::to_string(sid)] = neighbors;
            }
            j["data"] = data;
            j["count"] = idx.size();
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ═════════════════════════════════════════════════
    // 旅客端点
    // ═════════════════════════════════════════════════

    // ── GET /api/trains/query — 查票（直达+换乘）──
    app.Get("/api/trains/query", [](const httplib::Request& req, httplib::Response& res) {
        try {
            // JWT 鉴权
            auto ctx = checkAuth(req, res, Permission::QUERY_TRAINS);
            if (!ctx) return;

            // 解析逗号分隔的站 ID（支持城市级别查询）
            std::vector<uint32_t> from_ids, to_ids;
            std::string date;
            try {
                auto parseIds = [](const std::string& s) {
                    std::vector<uint32_t> ids;
                    size_t start = 0, end;
                    while ((end = s.find(',', start)) != std::string::npos) {
                        ids.push_back(std::stoul(s.substr(start, end - start)));
                        start = end + 1;
                    }
                    ids.push_back(std::stoul(s.substr(start)));
                    return ids;
                };
                if (req.has_param("from")) from_ids = parseIds(req.get_param_value("from"));
                if (req.has_param("to")) to_ids = parseIds(req.get_param_value("to"));
                date = req.has_param("date") ? req.get_param_value("date") : "2026-07-07";
            } catch (const std::exception&) {
                json j;
                j["ok"] = false;
                j["error"] = "Invalid from/to parameter";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            if (from_ids.empty() || to_ids.empty()) {
                json j;
                j["ok"] = false;
                j["error"] = "from and to station IDs are required";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }
            if (!isFuture(date, MAX_ADVANCE_DAYS)) {
                json j;
                j["ok"] = false;
                j["error"] = "Date must be within " + std::to_string(MAX_ADVANCE_DAYS) + " days from today";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            // 多站查询：每个 from×to 对分别查，合并去重
            auto& ds = DataStore::instance();
            QueryResult qr;
            std::set<std::string> seen;  // 去重 key = train_id|from|to
            for (auto fid : from_ids) {
                for (auto tid : to_ids) {
                    if (fid == tid) continue;
                    auto part = TrainQuery::query(fid, tid, date);
                    for (auto& item : part.direct) {
                        std::string key = item.train_id + "|" + std::to_string(item.from_station)
                                        + "|" + std::to_string(item.to_station);
                        if (!seen.insert(key).second) continue;
                        qr.direct.push_back(std::move(item));
                    }
                    for (auto& item : part.transfers) {
                        std::string key = item.train_id + "|" + std::to_string(item.from_station)
                                        + "|" + std::to_string(item.to_station);
                        if (!seen.insert(key).second) continue;
                        qr.transfers.push_back(std::move(item));
                    }
                }
            }

            json j;
            j["ok"] = true;
            j["direct_count"] = qr.direct.size();
            j["transfer_count"] = qr.transfers.size();

            // 席位价格（从费率矩阵按元/km计算，费率为0的席位不输出）
            auto addSeatPrices = [](json& target, const std::string& key,
                                     const std::string& train_id, double distance_km) {
                json sp;
                auto& cfg = SystemConfig::instance();
                auto add = [&](const char* name, SeatType st) {
                    double rate = cfg.ratePerKm(train_id, st);
                    if (rate > 0)
                        sp[name] = std::round(distance_km * rate * 100) / 100;
                };
                add("BUSINESS",     SeatType::BUSINESS);
                add("FIRST",        SeatType::FIRST);
                add("SECOND",       SeatType::SECOND);
                add("HARD_SLEEPER", SeatType::HARD_SLEEPER);
                add("HARD_SEAT",    SeatType::HARD_SEAT);
                add("NO_SEAT",      SeatType::NO_SEAT);
                target[key] = sp;
            };

            json direct_arr = json::array();
            for (const auto& item : qr.direct) {
                json d;
                d["train_id"] = item.train_id;
                d["from_station"] = item.from_station;
                d["to_station"] = item.to_station;
                d["departure_time"] = item.departure_time;
                d["arrival_time"] = item.arrival_time;
                d["duration_minutes"] = item.duration_minutes;
                d["distance_km"] = item.distance_km;
                d["price"] = item.price;
                d["available_seats"] = item.available_seats;
                // 始发站 / 终到站
                if (!item.stops.empty()) {
                    auto* orig = ds.getStation(item.stops.front().station_id);
                    auto* term = ds.getStation(item.stops.back().station_id);
                    d["origin_station"] = orig ? orig->name : "?";
                    d["terminal_station"] = term ? term->name : "?";
                }
                addSeatPrices(d, "seat_prices", item.train_id, item.distance_km);
                // 停站详情（含站名和时间，前端展示用）
                d["stops"] = stopsToJson(item.stops, ds);
                direct_arr.push_back(d);
            }
            j["direct"] = direct_arr;

            json transfer_arr = json::array();
            for (const auto& item : qr.transfers) {
                json j;
                j["is_transfer"] = true;
                j["train_id"] = item.train_id;
                j["from_station"] = item.from_station;
                j["to_station"] = item.to_station;
                j["second_train_id"] = item.second_train_id;
                j["transfer_station"] = item.transfer_station;
                j["transfer_arrival_time"] = item.transfer_arrival_time;
                j["transfer_departure_time"] = item.transfer_departure_time;
                j["transfer_gap_minutes"] = item.transfer_gap_minutes;
                j["departure_time"] = item.departure_time;
                j["arrival_time"] = item.arrival_time;
                j["duration_minutes"] = item.duration_minutes;
                j["distance_km"] = item.distance_km;
                j["price"] = item.price;
                // 始发/终到 + 各席位票价
                if (!item.stops.empty()) {
                    auto* orig = ds.getStation(item.stops.front().station_id);
                    auto* term = ds.getStation(item.stops.back().station_id);
                    j["origin_station"] = orig ? orig->name : "?";
                    j["terminal_station"] = term ? term->name : "?";
                }
                j["first_leg_seats"] = item.first_leg_seats;
                j["second_leg_seats"] = item.second_leg_seats;
                j["first_leg_price"] = item.first_leg_price;
                j["second_leg_price"] = item.second_leg_price;
                addSeatPrices(j, "seat_prices", item.train_id, item.distance_km);
                // 每程独立票价
                addSeatPrices(j, "first_leg_seat_prices", item.train_id, item.distance_km);
                addSeatPrices(j, "second_leg_seat_prices", item.second_train_id, item.distance_km);
                // 停站详情（第一段 + 第二段）
                
                j["stops"] = stopsToJson(item.stops, ds);
                j["second_stops"] = stopsToJson(item.second_stops, ds);
                transfer_arr.push_back(j);
            }
            j["transfers"] = transfer_arr;

            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── GET /api/trains/station — 车站查询 ──
    app.Get("/api/trains/station", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::QUERY_TRAINS);
            if (!ctx) return;

            auto& ds = DataStore::instance();
            std::string station_param = req.get_param_value("station");
            if (station_param.empty()) {
                json j;
                j["ok"] = false;
                j["error"] = "请输入车站名或城市名";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            // 解析逗号分隔的车站 ID（前端 resolveStationIds 已统一完成城市→ID 转换）
            std::vector<uint32_t> target_ids;
            {
                std::istringstream iss(station_param);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    try {
                        target_ids.push_back(static_cast<uint32_t>(std::stoul(token)));
                    } catch (...) { /* 跳过无效 ID */ }
                }
            }
            if (target_ids.empty()) {
                json j;
                j["ok"] = false;
                j["error"] = "未找到该车站或城市";
                res.set_content(j.dump(), "application/json");
                res.status = 404;
                return;
            }

            // 多站查询（后端自动合并同车次 + 排序）
            std::string sort = req.get_param_value("sort");
            if (sort.empty()) sort = "departure";
            auto items = TrainQuery::queryByStations(target_ids, sort);

            json all_items = json::array();
            for (auto& item : items) {
                json j;
                j["train_id"] = item.train_id;
                j["train_type"] = static_cast<int>(item.train_type);
                j["from_station_name"] = item.from_station_name;
                j["to_station_name"] = item.to_station_name;
                j["arrival_time"] = item.arrival_time;
                j["departure_time"] = item.departure_time;
                j["stops"] = stopsToJson(item.stops, ds);
                j["station_id"] = item.station_id;
                j["station_name"] = item.station_name;
                all_items.push_back(j);
            }

            json j;
            j["ok"] = true;
            j["count"] = all_items.size();
            j["data"] = all_items;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── GET /api/trains/{id}/stops — 列车经停站详情 ──
    app.Get(R"(/api/trains/([^/]+)/stops)", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::QUERY_TRAINS);
            if (!ctx) return;

            auto& ds = DataStore::instance();
            std::string train_id = req.matches[1];
            auto* train = ds.getTrain(train_id);
            if (!train) {
                json j;
                j["ok"] = false;
                j["error"] = "Train not found";
                res.set_content(j.dump(), "application/json");
                res.status = 404;
                return;
            }

            json j;
            j["ok"] = true;
            j["train_id"] = train_id;
            j["stops"] = stopsToJson(train->stops, ds);
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── POST /api/orders — 购票 ──
    app.Post("/api/orders", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::BUY_TICKETS);
            if (!ctx) return;

            json body = json::parse(req.body);
            auto result = OrderService::instance().createOrder(
                ctx->user_id,
                body.value("train_id", ""),
                body.value("date", "2026-07-07"),
                body.value("from_station", 0),
                body.value("to_station", 0),
                body.value("seat_type", SeatType::SECOND),
                body.value("count", 1),
                body.value("passenger_name", ""),
                body.value("passenger_id", "")
            );

            if (!result.order) {
                json j;
                j["ok"] = false;
                j["error"] = result.error;
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            json j;
            j["ok"] = true;
            j["order_id"] = result.order->id;
            j["train_id"] = result.order->train_id;
            j["seat_number"] = result.order->seat_number;
            j["price"] = result.order->price;
            j["status"] = result.order->status;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── GET /api/orders — 订单查询 ──
    app.Get("/api/orders", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::VIEW_OWN_ORDERS);
            if (!ctx) return;

            std::optional<OrderStatus> status_filter;
            if (req.has_param("status")) {
                std::string s = req.get_param_value("status");
                if (s == "PAID") status_filter = OrderStatus::PAID;
                else if (s == "REFUNDED") status_filter = OrderStatus::REFUNDED;
            }

            auto orders = OrderService::instance().getOrders(ctx->user_id, status_filter);

            // 为每个订单附加列车时刻信息
            auto& ds = DataStore::instance();
            json arr = json::array();
            for (const auto& order : orders) {
                json o = order;  // NLOHMANN_DEFINE_TYPE 自动序列化
                auto* train = ds.getTrain(order.train_id);
                if (train) {
                    int dep = 0, arr = 0, dur = 0;
                    for (size_t si = 0; si < train->stops.size(); ++si) {
                        if (train->stops[si].station_id == order.from_station) dep = train->stops[si].departure;
                        if (train->stops[si].station_id == order.to_station) {
                            arr = train->stops[si].arrival;
                            break;
                        }
                    }
                    dur = timeDiff(dep, arr);
                    o["departure_time"] = dep;
                    o["arrival_time"] = arr;
                    o["duration_minutes"] = dur;
                    auto* fromSt = ds.getStation(order.from_station);
                    auto* toSt = ds.getStation(order.to_station);
                    o["from_station_name"] = fromSt ? fromSt->name : "?";
                    o["to_station_name"] = toSt ? toSt->name : "?";
                    // 停站数据（供详情弹窗用）
                    json stopsArr = stopsToJson(train->stops, ds);
                    o["stops"] = stopsArr;
                }
                arr.push_back(o);
            }

            json j;
            j["ok"] = true;
            j["count"] = orders.size();
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

    // ── POST /api/orders/{id}/refund — 退票 ──
    app.Post(R"(/api/orders/([^/]+)/refund)", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::REFUND_OWN);
            if (!ctx) return;

            std::string order_id = req.matches[1];
            auto result = OrderService::instance().refundOrder(order_id, ctx->user_id);

            if (!result.refund_amount) {
                json j;
                j["ok"] = false;
                j["error"] = result.error;
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            json j;
            j["ok"] = true;
            j["refund_amount"] = *result.refund_amount;
            j["order_id"] = order_id;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ═══════════════════════════════════════════
    // 职工端 — 列车管理 + 审批
    // ═══════════════════════════════════════════

    // ── GET /api/admin/trains — 列车列表 ──
    app.Get("/api/admin/trains", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
            if (!ctx) return;

            auto& trains = TrainManager::instance().getAllTrains();
            json arr = json::array();
            for (const auto& train : trains) {
                json jt;
                jt["id"] = train.id;
                jt["type"] = static_cast<int>(train.type);
                jt["status"] = static_cast<int>(train.status);
                jt["stops_count"] = train.stops.size();
                jt["stops"] = stopsToJson(train.stops, DataStore::instance());
                jt["segments"] = buildSegments(train, DataStore::instance());
                arr.push_back(jt);
            }

            json j;
            j["ok"] = true;
            j["count"] = arr.size();
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

    // ── POST /api/admin/trains — 新增列车（提交审批）──
    // ── PUT  /api/admin/trains/{id} — 修改列车（与新增共用逻辑，is_new 由 URL 是否有 id 决定）──
    auto handleTrainSubmit = [](const httplib::Request& req, httplib::Response& res, bool is_new) {
        try {
            auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
            if (!ctx) return;

            json body = json::parse(req.body);
            Train train = body.get<Train>();

            // 校验 + 冲突检测（提交/审批共用 checkTrain）
            auto cr = TrainManager::instance().checkTrain(train, is_new);
            if (!cr.valid) {
                json j;
                j["ok"] = false;
                j["error"] = cr.error;
                if (!cr.conflicts.empty()) {
                    json details = json::array();
                    for (const auto& c : cr.conflicts) {
                        json cd;
                        cd["train_id"] = c.train_id;
                        cd["station_a"] = c.station_a;
                        cd["station_b"] = c.station_b;
                        cd["line_id"] = c.line_id;
                        cd["conflicting_enter"] = c.conflicting_enter;
                        cd["conflicting_leave"] = c.conflicting_leave;
                        details.push_back(cd);
                    }
                    j["conflicts"] = details;
                }
                res.set_content(j.dump(), "application/json");
                res.status = cr.conflicts.empty() ? 400 : 409;
                return;
            }

            // 提交审批
            auto type = is_new ? ApprovalType::CREATE_TRAIN : ApprovalType::ADJUST_SCHEDULE;
            std::string payload;
            if (is_new) {
                // CREATE_TRAIN：立即写入 trains.json（PENDING），审批只存车次号
                train.status = TrainStatus::PENDING;
                auto& ds = DataStore::instance();
                // 已归档同名车次 → 先删除再新增
                ds.removeTrain(train.id);
                ds.addTrain(train);
                ds.saveTrains();
                WalWriter::instance().append("TRAIN_CREATE", json(train).dump());
                payload = json{{"id", train.id}}.dump();
            } else {
                // ADJUST_SCHEDULE：payload 存完整 stops（审批时需应用新数据）
                payload = body.dump();
            }
            std::string aid = ApprovalService::instance().submit(
                type, ctx->user_id, payload);

            json j;
            j["ok"] = true;
            j["approval_id"] = aid;
            j["message"] = "已提交审批";
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    };

    app.Post("/api/admin/trains", [handleTrainSubmit](const httplib::Request& req, httplib::Response& res) {
        handleTrainSubmit(req, res, true);
    });

    app.Put(R"(/api/admin/trains/([^/]+))", [handleTrainSubmit](const httplib::Request& req, httplib::Response& res) {
        // 校验 URL 中的 train ID 与请求体一致，防止修改错列车
        try {
            json body = json::parse(req.body);
            std::string url_id = req.matches[1];
            std::string body_id = body.value("id", "");
            if (url_id != body_id) {
                json j;
                j["ok"] = false;
                j["error"] = "URL 与请求体中的车次号不一致";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }
        } catch (const std::exception&) {
            json j;
            j["ok"] = false;
            j["error"] = "请求体 JSON 格式错误";
            res.set_content(j.dump(), "application/json");
            res.status = 400;
            return;
        }
        handleTrainSubmit(req, res, false);
    });

    // ── DELETE /api/admin/trains/{id} — 删除列车（提交审批）──
    app.Delete(R"(/api/admin/trains/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
            if (!ctx) return;

            std::string train_id = req.matches[1];
            auto& ds = DataStore::instance();
            auto* train = ds.getTrain(train_id);
            if (!train) {
                json j;
                j["ok"] = false;
                j["error"] = "列车不存在";
                res.set_content(j.dump(), "application/json");
                res.status = 404;
                return;
            }

            std::string del_date = req.get_param_value("date");

            // 删除日期须 ≥15 天（14 天内的票已放出）
            if (!del_date.empty()) {
                if (!isFuture(del_date, 365)) {
                    json j;
                    j["ok"] = false;
                    j["error"] = "日期不能是过去";
                    res.set_content(j.dump(), "application/json");
                    res.status = 400;
                    return;
                }
                if (isFuture(del_date, MAX_ADVANCE_DAYS)) {
                    json j;
                    j["ok"] = false;
                    j["error"] = "删除日期须至少 15 天后（第14天已放票）";
                    res.set_content(j.dump(), "application/json");
                    res.status = 400;
                    return;
                }
            }

            json payload;
            payload["id"] = train_id;
            if (!del_date.empty()) payload["delete_date"] = del_date;
            std::string aid = ApprovalService::instance().submit(
                ApprovalType::DELETE_TRAIN, ctx->user_id, payload.dump());

            json j;
            j["ok"] = true;
            j["approval_id"] = aid;
            j["message"] = "已提交审批";
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── GET /api/admin/approvals — 审批列表（STAFF 看自己提交 / APPROVER 看所有+审批记录）──
    app.Get("/api/admin/approvals", [](const httplib::Request& req, httplib::Response& res) {
        try {
            // STAFF 和 APPROVER 均可访问（STAFF 只能看自己的提交）
            auto ctx = RbacMiddleware::authenticate(
                req.has_header("Authorization") ? req.get_header_value("Authorization") : "");
            if (!ctx || (!RbacMiddleware::authorize(*ctx, Permission::APPROVE)
                      && !RbacMiddleware::authorize(*ctx, Permission::MANAGE_TRAINS))) {
                json j;
                j["ok"] = false;
                j["error"] = ctx ? "Forbidden" : "Unauthorized";
                res.set_content(j.dump(), "application/json");
                res.status = ctx ? 403 : 401;
                return;
            }

            std::string status = req.get_param_value("status");
            std::optional<ApprovalState> filter;
            if (status == "SUBMITTED") filter = ApprovalState::SUBMITTED;
            else if (status == "APPROVED") filter = ApprovalState::APPROVED;
            else if (status == "REJECTED") filter = ApprovalState::REJECTED;
            else if (status == "WITHDRAWN") filter = ApprovalState::WITHDRAWN;

            std::string submitter_id = req.get_param_value("submitter_id");
            std::string approver_id = req.get_param_value("approver_id");

            // 如果传入的是用户名而非 UUID，解析为 user_id（前端可能只有 username）
            auto& auth = AuthService::instance();
            if (!submitter_id.empty()) {
                auto* u = auth.findUser(submitter_id);  // 先当 username 查
                if (u) submitter_id = u->id;             // 转为 UUID
            }
            if (!approver_id.empty()) {
                auto* u = auth.findUser(approver_id);
                if (u) approver_id = u->id;
            }

            auto approvals = ApprovalService::instance().getApprovals(filter);
            json arr = json::array();
            for (const auto& a : approvals) {
                if (!submitter_id.empty() && a.submitter_id != submitter_id) continue;
                if (!approver_id.empty() && a.approver_id != approver_id) continue;

                json ja;
                ja["id"] = a.id;
                ja["type"] = static_cast<int>(a.type);
                ja["submitter_id"] = a.submitter_id;
                ja["approver_id"] = a.approver_id;
                ja["status"] = static_cast<int>(a.status);
                ja["submitted_at"] = a.submitted_at;
                ja["decided_at"] = a.decided_at;
                ja["comment"] = a.comment;
                try { ja["payload"] = json::parse(a.payload); } catch (...) { ja["payload"] = json(); }
                // 为 CREATE_TRAIN / ADJUST_SCHEDULE 补齐 segments（前端展示里程/速度用）
                if ((a.type == ApprovalType::CREATE_TRAIN || a.type == ApprovalType::ADJUST_SCHEDULE)
                    && ja["payload"].contains("stops")) {
                    Train temp;
                    temp.stops = ja["payload"]["stops"].get<std::vector<Stop>>();
                    ja["payload"]["segments"] = buildSegments(temp, DataStore::instance());
                }
                arr.push_back(ja);
            }

            json j;
            j["ok"] = true;
            j["count"] = arr.size();
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

    // ── POST /api/admin/approvals/{id}/approve — 审批通过 ──
    app.Post(R"(/api/admin/approvals/([^/]+)/approve)", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::APPROVE);
            if (!ctx) return;

            std::string approval_id = req.matches[1];
            auto result = ApprovalService::instance().approve(approval_id, ctx->user_id);
            json j;
            j["ok"] = result.success;
            if (!result.success) {
                j["error"] = result.error;
                res.status = 400;
            } else {
                j["train_id"] = result.train_id;
                j["message"] = "审批通过，列车已生效";
            }
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── POST /api/admin/approvals/{id}/reject — 审批驳回 ──
    app.Post(R"(/api/admin/approvals/([^/]+)/reject)", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::APPROVE);
            if (!ctx) return;

            std::string approval_id = req.matches[1];
            json body = json::parse(req.body);
            std::string comment = body.value("comment", "");
            if (comment.empty()) {
                json j;
                j["ok"] = false;
                j["error"] = "驳回时必须填写意见";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            auto result = ApprovalService::instance().reject(approval_id, ctx->user_id, comment);
            json j;
            j["ok"] = result.success;
            if (!result.success) j["error"] = result.error;
            else j["message"] = "已驳回";
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    // ── POST /api/admin/approvals/{id}/withdraw — 提交人撤回 ──
    app.Post(R"(/api/admin/approvals/([^/]+)/withdraw)", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
            if (!ctx) return;

            std::string approval_id = req.matches[1];
            auto result = ApprovalService::instance().withdraw(approval_id, ctx->user_id);
            json j;
            j["ok"] = result.success;
            if (!result.success) j["error"] = result.error;
            else j["message"] = "已撤回";
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"] = e.what();
            res.set_content(j.dump(), "application/json");
            res.status = 500;
        }
    });

    Logger::instance().info("Routes registered: 18 endpoints (auth, passenger, staff, debug)");
}
