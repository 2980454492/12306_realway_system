// routes.cpp — 路由注册实现
#include "core/routes.h"
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

#include <nlohmann/json.hpp>

#include <chrono>
#include <sstream>

using json = nlohmann::json;  // 局部 using，非全局

/** 停站序列 → JSON 数组 [{station_id, station_name, arrival, departure}] */
inline json stopsToJson(const std::vector<Stop>& stops, DataStore& ds) {
    json arr = json::array();
    for (const auto& stop : stops) {
        json sd;
        sd["station_id"] = stop.station_id;
        auto* st = ds.getStation(stop.station_id);
        sd["station_name"] = st ? st->name : "?";
        sd["arrival"] = stop.arrival;
        sd["departure"] = stop.departure;
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

    // ── 静态文件托管 — frontend/ 目录 ──
    // 所有非 API 路径由前端 SPA 处理
    // index.html 作为兜底页面，SPA 路由由 JS 的 hash-based router 接管
    app.set_mount_point("/", "frontend");

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
                case UserRole::APPROVER:  role_str = "APPROVER"; break;
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

            // 席位价格换算（二等座价格为基准，各席位按倍率换算）
            auto addSeatPrices = [](json& target, const std::string& key, double base_price) {
                json sp;
                sp["BUSINESS"]     = base_price * seatPriceMultiplier(SeatType::BUSINESS);
                sp["FIRST"]        = base_price * seatPriceMultiplier(SeatType::FIRST);
                sp["SECOND"]       = base_price;
                sp["HARD_SLEEPER"] = base_price * seatPriceMultiplier(SeatType::HARD_SLEEPER);
                sp["HARD_SEAT"]    = base_price * seatPriceMultiplier(SeatType::HARD_SEAT);
                sp["NO_SEAT"]      = base_price * seatPriceMultiplier(SeatType::NO_SEAT);
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
                addSeatPrices(d, "seat_prices", item.price);
                // 停站详情（含站名和时间，前端展示用）
                d["stops"] = stopsToJson(item.stops, ds);
                direct_arr.push_back(d);
            }
            j["direct"] = direct_arr;

            json transfer_arr = json::array();
            for (const auto& item : qr.transfers) {
                json t;
                t["is_transfer"] = true;
                t["train_id"] = item.train_id;
                t["from_station"] = item.from_station;
                t["to_station"] = item.to_station;
                t["second_train_id"] = item.second_train_id;
                t["transfer_station"] = item.transfer_station;
                t["transfer_arrival_time"] = item.transfer_arrival_time;
                t["transfer_departure_time"] = item.transfer_departure_time;
                t["transfer_gap_minutes"] = item.transfer_gap_minutes;
                t["departure_time"] = item.departure_time;
                t["arrival_time"] = item.arrival_time;
                t["duration_minutes"] = item.duration_minutes;
                t["distance_km"] = item.distance_km;
                t["price"] = item.price;
                // 始发/终到 + 各席位票价
                if (!item.stops.empty()) {
                    auto* orig = ds.getStation(item.stops.front().station_id);
                    auto* term = ds.getStation(item.stops.back().station_id);
                    t["origin_station"] = orig ? orig->name : "?";
                    t["terminal_station"] = term ? term->name : "?";
                }
                t["first_leg_seats"] = item.first_leg_seats;
                t["second_leg_seats"] = item.second_leg_seats;
                t["first_leg_price"] = item.first_leg_price;
                t["second_leg_price"] = item.second_leg_price;
                addSeatPrices(t, "seat_prices", item.price);
                // 每程独立票价
                addSeatPrices(t, "first_leg_seat_prices", item.first_leg_price);
                addSeatPrices(t, "second_leg_seat_prices", item.second_leg_price);
                // 停站详情（第一段 + 第二段）
                
                t["stops"] = stopsToJson(item.stops, ds);
                t["second_stops"] = stopsToJson(item.second_stops, ds);
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
                json t;
                t["train_id"] = item.train_id;
                t["train_type"] = static_cast<int>(item.train_type);
                t["from_station_name"] = item.from_station_name;
                t["to_station_name"] = item.to_station_name;
                t["arrival_time"] = item.arrival_time;
                t["departure_time"] = item.departure_time;
                t["stops"] = stopsToJson(item.stops, ds);
                t["station_id"] = item.station_id;
                t["station_name"] = item.station_name;
                all_items.push_back(t);
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
            for (const auto& t : trains) {
                json jt;
                jt["id"] = t.id;
                jt["type"] = static_cast<int>(t.type);
                jt["status"] = static_cast<int>(t.status);
                jt["stops_count"] = t.stops.size();
                jt["stops"] = stopsToJson(t.stops, DataStore::instance());
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
    app.Post("/api/admin/trains", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
            if (!ctx) return;

            json body = json::parse(req.body);
            Train train = body.get<Train>();
            // 前端未传 route_stations 时，从 stops 自动填充
            if (train.route_stations.empty()) {
                for (const auto& s : train.stops) train.route_stations.push_back(s.station_id);
            }

            // 校验
            auto vr = TrainManager::instance().validate(train, true);
            if (!vr.valid) {
                json j;
                j["ok"] = false;
                j["error"] = vr.error;
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            // 冲突检测
            auto conflicts = TrainManager::instance().detectConflicts(train);
            if (!conflicts.empty()) {
                json j;
                j["ok"] = false;
                j["error"] = "运行图冲突：与 " + conflicts[0].train_id + " 在区间重叠";
                json details = json::array();
                for (const auto& c : conflicts) {
                    json cd;
                    cd["train_id"] = c.train_id;
                    cd["station_a"] = c.station_a;
                    cd["station_b"] = c.station_b;
                    cd["conflicting_enter"] = c.conflicting_enter;
                    cd["conflicting_leave"] = c.conflicting_leave;
                    details.push_back(cd);
                }
                j["conflicts"] = details;
                res.set_content(j.dump(), "application/json");
                res.status = 409;
                return;
            }

            // 提交审批
            std::string snapshot = body.dump();
            std::string aid = ApprovalService::instance().submit(
                ApprovalType::CREATE_TRAIN, ctx->user_id, body.dump(), "");

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

            json payload;
            payload["id"] = train_id;
            std::string aid = ApprovalService::instance().submit(
                ApprovalType::DELETE_TRAIN, ctx->user_id, payload.dump(), "");

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

    // ── PUT /api/admin/trains/{id}/schedule — 调整时刻（提交审批）──
    app.Put(R"(/api/admin/trains/([^/]+)/schedule)", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto ctx = checkAuth(req, res, Permission::MANAGE_TRAINS);
            if (!ctx) return;

            std::string train_id = req.matches[1];
            json body = json::parse(req.body);
            auto new_stops = body["stops"].get<std::vector<Stop>>();

            // 构建临时列车做校验
            auto& ds = DataStore::instance();
            auto* existing = ds.getTrain(train_id);
            if (!existing) {
                json j;
                j["ok"] = false;
                j["error"] = "列车不存在";
                res.set_content(j.dump(), "application/json");
                res.status = 404;
                return;
            }

            Train temp = *existing;
            temp.stops = new_stops;
            auto vr = TrainManager::instance().validate(temp, false);
            if (!vr.valid) {
                json j;
                j["ok"] = false;
                j["error"] = vr.error;
                res.set_content(j.dump(), "application/json");
                res.status = 400;
                return;
            }

            // 冲突检测
            auto conflicts = TrainManager::instance().detectConflicts(temp);
            if (!conflicts.empty()) {
                json j;
                j["ok"] = false;
                j["error"] = "运行图冲突：与 " + conflicts[0].train_id + " 在区间重叠";
                json details = json::array();
                for (const auto& c : conflicts) {
                    json cd;
                    cd["train_id"] = c.train_id;
                    cd["station_a"] = c.station_a;
                    cd["station_b"] = c.station_b;
                    details.push_back(cd);
                }
                j["conflicts"] = details;
                res.set_content(j.dump(), "application/json");
                res.status = 409;
                return;
            }

            // 提交审批
            json payload;
            payload["id"] = train_id;
            payload["stops"] = body["stops"];
            std::string aid = ApprovalService::instance().submit(
                ApprovalType::ADJUST_SCHEDULE, ctx->user_id, payload.dump(), "");

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
            else if (status == "EXPIRED") filter = ApprovalState::EXPIRED;

            std::string submitter_id = req.get_param_value("submitter_id");
            std::string approver_id = req.get_param_value("approver_id");

            auto approvals = ApprovalService::instance().getApprovals(filter);
            json arr = json::array();
            for (const auto& a : approvals) {
                // submitter_id 筛选（STAFF 只能看自己的提交）
                if (!submitter_id.empty() && a.submitter_id != submitter_id) continue;
                // approver_id 筛选（查看审批人的历史记录）
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

    Logger::instance().info("Routes registered: 16 endpoints (auth, passenger, staff, debug)");
}
