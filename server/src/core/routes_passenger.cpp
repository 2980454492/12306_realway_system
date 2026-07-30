// This file is part of routes.cpp — auto-split by module
#include "core/routes_helpers.h"

void registerPassengerRoutes(RailwayServer& server) {
    auto& app = server.getApp();
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
    if (!checkRateLimit("query:" + clientIP(req), 120, 120.0/60, res)) return;
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
                } catch (...) {
                    // 跳过无效 ID
                }
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
    if (!checkRateLimit("buy:" + clientIP(req), 10, 10.0/60, res)) return;
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
            // 解密并脱敏身份证号（如 37****199001010011）
            std::string raw_id = crypto::decrypt(order.passenger_id).value_or(order.passenger_id);
            if (raw_id.length() >= 4)
                o["passenger_id"] = raw_id.substr(0, 2) + std::string(raw_id.length() - 4, '*') + raw_id.substr(raw_id.length() - 2);
            else
                o["passenger_id"] = raw_id;
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

}
