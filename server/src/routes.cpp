// routes.cpp — 路由注册实现
#include "routes.h"
#include "server.h"
#include "logger.h"
#include "data_store.h"
#include "railway_graph.h"
#include "auth_service.h"
#include "jwt_service.h"
#include "rbac_middleware.h"
#include "train_query.h"
#include "order_service.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>

using json = nlohmann::json;  // 局部 using，非全局

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

    // ── GET / — 欢迎页 ──
    // 浏览器直接打开可确认服务正在运行
    // 临时内嵌 HTML，Phase 5 迁移到 server/frontend/ 用 set_mount_point 托管
    app.Get("/", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(R"(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>12306 铁路票务系统</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
         background: #1a1a2e; color: #e0e0e0; display: flex; justify-content: center;
         align-items: center; min-height: 100vh; }
  .card { background: #16213e; border: 1px solid #0f3460; border-radius: 12px;
          padding: 48px 64px; text-align: center; max-width: 520px; }
  h1 { font-size: 28px; color: #e94560; margin-bottom: 8px; }
  .subtitle { color: #a0a0b0; font-size: 14px; margin-bottom: 32px; }
  .status { display: inline-flex; align-items: center; gap: 8px; background: #0f3460;
            padding: 10px 24px; border-radius: 24px; font-size: 14px; margin-bottom: 32px; }
  .dot { width: 10px; height: 10px; background: #00ff88; border-radius: 50%;
         animation: pulse 2s infinite; }
  @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.3; } }
  .links { display: flex; gap: 12px; justify-content: center; flex-wrap: wrap; }
  .links a { color: #53a8ff; text-decoration: none; font-size: 13px; padding: 6px 16px;
             border: 1px solid #0f3460; border-radius: 6px; transition: all .2s; }
  .links a:hover { background: #0f3460; color: #fff; }
  .footer { margin-top: 36px; color: #606080; font-size: 12px; }
</style>
</head>
<body>
<div class="card">
  <h1>12306 铁路票务系统</h1>
  <p class="subtitle">Railway Ticketing System — C++17</p>
  <div class="status"><span class="dot"></span> 服务运行中</div>
  <div class="links">
    <a href="/health">/health</a>
    <a href="/api/trains/query">/api/trains/query</a>
    <a href="/api/orders">/api/orders</a>
    <a href="/api/admin/trains">/api/admin/trains</a>
  </div>
  <p class="footer">Phase 1 · v0.1.0</p>
</div>
</body>
</html>
        )", "text/html; charset=utf-8");
    });

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

    // ── GET /api/debug/graph — 查询最短路径（调试验证用）──
    app.Get("/api/debug/graph", [](const httplib::Request& req, httplib::Response& res) {
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

            // 解析查询参数
            uint32_t from = 0, to = 0;
            try {
                if (req.has_param("from")) {
                    from = std::stoul(req.get_param_value("from"));
                }
                if (req.has_param("to")) {
                    to = std::stoul(req.get_param_value("to"));
                }
            } catch (const std::exception&) {
                json j;
                j["ok"] = false;
                j["error"] = "Invalid from/to parameter: must be a valid station ID";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            if (from == 0 || to == 0) {
                json j;
                j["ok"] = false;
                j["error"] = "Missing from/to parameter (station ID)";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            // 构建图并查询最短路径
            RailwayGraph graph;
            graph.build(ds.getAllLines());

            auto path = graph.shortestPath(from, to);

            json j;
            j["ok"] = true;
            j["from"] = from;
            j["to"] = to;
            j["found"] = path.found;
            if (path.found) {
                j["distance_km"] = path.total_distance_km;
                // 将站点 ID 转为站名，方便阅读
                json route = json::array();
                for (auto sid : path.stations) {
                    auto* station = ds.getStation(sid);
                    if (station) {
                        route.push_back({{"id", sid}, {"name", station->name}});
                    } else {
                        route.push_back({{"id", sid}, {"name", "unknown"}});
                    }
                }
                j["route"] = route;
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

    // ── POST /api/auth/login — 登录（调试验证用，Phase 3 正式实现 JWT）──
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
                case UserRole::ADMIN:     role_str = "ADMIN"; break;
                case UserRole::STAFF:     role_str = "STAFF"; break;
                case UserRole::PASSENGER: role_str = "PASSENGER"; break;
            }
            std::string token = JwtService::instance().generateToken(
                user->id, role_str, 1800);

            json j;
            j["ok"] = true;
            j["token"] = token;
            j["token_type"] = "Bearer";
            j["expires_in"] = 1800;
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
            std::string auth = req.has_header("Authorization")
                ? req.get_header_value("Authorization") : "";

            auto ctx = RbacMiddleware::authenticate(auth);
            if (!ctx) {
                json j;
                j["ok"] = false;
                j["error"] = "Unauthorized";
                res.set_content(j.dump(), "application/json");
                res.status = 401;
                return;
            }

            // 检查 ADMIN 权限
            if (!RbacMiddleware::authorize(*ctx, Permission::MANAGE_USERS)) {
                json j;
                j["ok"] = false;
                j["error"] = "Forbidden: admin only";
                res.set_content(j.dump(), "application/json");
                res.status = 403;
                return;
            }

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

    // ═════════════════════════════════════════════════
    // 旅客端点
    // ═════════════════════════════════════════════════

    // ── GET /api/trains/query — 查票（直达+换乘）──
    app.Get("/api/trains/query", [](const httplib::Request& req, httplib::Response& res) {
        try {
            // JWT 鉴权
            std::string auth = req.has_header("Authorization")
                ? req.get_header_value("Authorization") : "";
            auto ctx = RbacMiddleware::authenticate(auth);
            if (!ctx || !RbacMiddleware::authorize(*ctx, Permission::QUERY_TRAINS)) {
                json j;
                j["ok"] = false;
                j["error"] = ctx ? "Forbidden" : "Unauthorized";
                res.set_content(j.dump(), "application/json");
                res.status = ctx ? 403 : 401;
                return;
            }

            uint32_t from = 0, to = 0;
            std::string date;
            try {
                if (req.has_param("from")) from = std::stoul(req.get_param_value("from"));
                if (req.has_param("to")) to = std::stoul(req.get_param_value("to"));
                date = req.has_param("date") ? req.get_param_value("date") : "2026-07-07";
            } catch (const std::exception&) {
                json j;
                j["ok"] = false;
                j["error"] = "Invalid from/to parameter";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            if (from == 0 || to == 0) {
                json j;
                j["ok"] = false;
                j["error"] = "from and to station IDs are required";
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            auto qr = TrainQuery::query(from, to, date);

            json j;
            j["ok"] = true;
            j["direct_count"] = qr.direct.size();
            j["transfer_count"] = qr.transfers.size();

            json direct_arr = json::array();
            for (const auto& item : qr.direct) {
                json d;
                d["train_id"] = item.train_id;
                d["departure_time"] = item.departure_time;
                d["arrival_time"] = item.arrival_time;
                d["duration_minutes"] = item.duration_minutes;
                d["price"] = item.price;
                d["available_seats"] = item.available_seats;
                direct_arr.push_back(d);
            }
            j["direct"] = direct_arr;

            json transfer_arr = json::array();
            for (const auto& item : qr.transfers) {
                json t;
                t["train_id"] = item.train_id;
                t["second_train_id"] = item.second_train_id;
                t["transfer_station"] = item.transfer_station;
                t["departure_time"] = item.departure_time;
                t["arrival_time"] = item.arrival_time;
                t["duration_minutes"] = item.duration_minutes;
                t["price"] = item.price;
                transfer_arr.push_back(t);
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

    // ── POST /api/orders — 购票 ──
    app.Post("/api/orders", [](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string auth = req.has_header("Authorization")
                ? req.get_header_value("Authorization") : "";
            auto ctx = RbacMiddleware::authenticate(auth);
            if (!ctx || !RbacMiddleware::authorize(*ctx, Permission::BUY_TICKETS)) {
                json j;
                j["ok"] = false;
                j["error"] = ctx ? "Forbidden" : "Unauthorized";
                res.set_content(j.dump(), "application/json");
                res.status = ctx ? 403 : 401;
                return;
            }

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
            std::string auth = req.has_header("Authorization")
                ? req.get_header_value("Authorization") : "";
            auto ctx = RbacMiddleware::authenticate(auth);
            if (!ctx || !RbacMiddleware::authorize(*ctx, Permission::VIEW_OWN_ORDERS)) {
                json j;
                j["ok"] = false;
                j["error"] = ctx ? "Forbidden" : "Unauthorized";
                res.set_content(j.dump(), "application/json");
                res.status = ctx ? 403 : 401;
                return;
            }

            std::optional<OrderStatus> status_filter;
            if (req.has_param("status")) {
                std::string s = req.get_param_value("status");
                if (s == "PAID") status_filter = OrderStatus::PAID;
                else if (s == "REFUNDED") status_filter = OrderStatus::REFUNDED;
                else if (s == "CANCELLED") status_filter = OrderStatus::CANCELLED;
            }

            auto orders = OrderService::instance().getOrders(ctx->user_id, status_filter);

            json j;
            j["ok"] = true;
            j["count"] = orders.size();
            j["data"] = orders;
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
            std::string auth = req.has_header("Authorization")
                ? req.get_header_value("Authorization") : "";
            auto ctx = RbacMiddleware::authenticate(auth);
            if (!ctx || !RbacMiddleware::authorize(*ctx, Permission::REFUND_OWN)) {
                json j;
                j["ok"] = false;
                j["error"] = ctx ? "Forbidden" : "Unauthorized";
                res.set_content(j.dump(), "application/json");
                res.status = ctx ? 403 : 401;
                return;
            }

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

    Logger::instance().info("Routes registered: 10 endpoints (/, /health, auth, passenger, admin, debug)");
}
