// Auto-split from router_admin.cpp
#include "router_helpers.h"

namespace {

/** 校验站名：非空且不与其他站点重复。excludeId=0 新增，非0 更新（排除自身）。
 *  返回空字符串 = 通过，非空 = 错误信息 */
std::string checkStationName(const std::string& name, uint32_t excludeId) {
    if (name.empty())
        return "站名不能为空";
    // O(1)：用预建索引 name→id 查重
    uint32_t existId = DataStore::instance().stationNameToId(name);
    if (existId != 0 && existId != excludeId)
        return "站名已存在: " + name;
    return "";
}

/** 校验线路请求体：名称、车站数、时速、车站存在。
 *  返回空字符串 = 通过，非空 = 错误信息 */
std::string checkLineBody(const json& body) {
    std::string name = body.value("name", "");
    if (name.empty())
        return "线路名称不能为空";
    if (!body.contains("stations") || !body["stations"].is_array() || body["stations"].size() < 2)
        return "至少需要起点和终点两个站点";
    if (body.value("max_speed_kmh", 0) <= 0)
        return "设计时速必须大于0";
    // 车站存在性检查：DataStore 初始化时已预建 name/city 集合，O(1) 查表
    const std::unordered_set<std::string>& valid = DataStore::instance().getStationNameSet();
    for (const json& s : body["stations"]) {
        std::string st = s.get<std::string>();
        if (!valid.count(st))
            return "车站不存在: " + st;
    }
    return "";
}

}  // namespace

void registerInfraAdminRoutes(RailwayServer& server) {
    httplib::Server& app = server.getApp();
    // ── 站点管理（INFRA_ADMIN）──

    /** GET /api/admin/stations — 获取全部站点列表 */
    app.Get("/api/admin/stations", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        json j;
        j["ok"] = true;
        j["data"] = DataStore::instance().getAllStations();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** POST /api/admin/stations — 新增站点 */
    app.Post("/api/admin/stations", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        json body = json::parse(req.body);

        std::string name = body.value("name", "");
        std::string err = checkStationName(name, 0);
        if (!err.empty()) {
            badRequest(res, err);
            return;
        }

        Station station;
        station.id = body.value("id", 0);
        station.name = name;
        station.city = body.value("city", "");
        station.type = body.value("type", StationType::NORMAL);
        station.latitude = body.value("latitude", 0.0);
        station.longitude = body.value("longitude", 0.0);
        DataStore::instance().addStation(station);
        json j;
        j["ok"] = true;
        j["data"] = station;
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** PUT /api/admin/stations/{id} — 修改站点 */
    app.Put(R"(/api/admin/stations/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "站点"))
            return;
        json body = json::parse(req.body);

        std::string name = body.value("name", "");
        std::string err = checkStationName(name, id);
        if (!err.empty()) {
            badRequest(res, err);
            return;
        }

        Station station;
        station.id = body.value("id", 0);
        station.name = name;
        station.city = body.value("city", "");
        station.type = body.value("type", StationType::NORMAL);
        station.latitude = body.value("latitude", 0.0);
        station.longitude = body.value("longitude", 0.0);
        if (!DataStore::instance().updateStation(id, station)) {
            json j; j["ok"] = false; j["error"] = "站点不存在";
            res.set_content(j.dump(), "application/json"); res.status = 404;
            return;
        }
        json j; j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** DELETE /api/admin/stations/{id} — 删除站点 */
    app.Delete(R"(/api/admin/stations/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "站点"))
            return;
        if (!DataStore::instance().removeStation(id)) {
            json j; j["ok"] = false; j["error"] = "站点不存在";
            res.set_content(j.dump(), "application/json"); res.status = 404;
            return;
        }
        json j; j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    // ── 线路管理（INFRA_ADMIN）──

    /** GET /api/admin/lines — 获取全部线路列表（INFRA_ADMIN + STAFF 均可访问） */
    app.Get("/api/admin/lines", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = RbacMiddleware::authenticate(
            req.has_header("Authorization") ? req.get_header_value("Authorization") : "");
        if (!ctx || (!RbacMiddleware::authorize(*ctx, Permission::MANAGE_LINES)
                  && !RbacMiddleware::authorize(*ctx, Permission::MANAGE_TRAINS))) {
            json j;
            j["ok"] = false;
            j["error"] = ctx ? "权限不足" : "未登录";
            res.set_content(j.dump(), "application/json");
            res.status = ctx ? 403 : 401;
            return;
        }
        json j;
        j["ok"] = true;
        j["data"] = DataStore::instance().getAllLines();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** POST /api/admin/lines — 新增线路 */
    app.Post("/api/admin/lines", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        json body = json::parse(req.body);

        std::string err = checkLineBody(body);
        if (!err.empty()) {
            badRequest(res, err);
            return;
        }

        Line line;
        line.id = body.value("id", 0);
        line.name = body.value("name", "");
        line.type = body.value("type", LineType::NORMAL);
        line.max_speed_kmh = body.value("max_speed_kmh", 0);
        for (const json& s : body["stations"])
            line.stations.push_back(s.get<std::string>());
        DataStore::instance().addLine(line);
        json j;
        j["ok"] = true;
        j["data"] = line;
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** PUT /api/admin/lines/{id} — 修改线路 */
    app.Put(R"(/api/admin/lines/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "线路"))
            return;
        json body = json::parse(req.body);

        std::string err = checkLineBody(body);
        if (!err.empty()) {
            badRequest(res, err);
            return;
        }

        // 保存旧站点列表用于比较（O(1) 索引查线路）
        Line* old_line = DataStore::instance().getLine(id);
        std::vector<std::string> old_stations = old_line ? old_line->stations : std::vector<std::string>{};

        Line line;
        line.id = body.value("id", 0);
        line.name = body.value("name", "");
        line.type = body.value("type", LineType::NORMAL);
        line.max_speed_kmh = body.value("max_speed_kmh", 0);
        for (const json& s : body["stations"])
            line.stations.push_back(s.get<std::string>());

        if (!DataStore::instance().updateLine(id, line)) {
            json j; 
            j["ok"] = false; 
            j["error"] = "线路不存在";
            res.set_content(j.dump(), "application/json"); 
            res.status = 404;
            return;
        }

        // 检测新增站点，为经过该线路的列车创建补站审批
        // 先算出被删的站点集合，供改站检测用
        std::set<std::string> removed_set;
        for (const std::string& old_st : old_stations)
            if (std::find(line.stations.begin(), line.stations.end(), old_st) == line.stations.end())
                removed_set.insert(old_st);

        json affected_trains = json::array();
        DataStore& ds = DataStore::instance();
        std::set<std::string> paired_removed;  // 已配对为改站的被删站点
        for (size_t pos = 0; pos < line.stations.size(); pos++) {
            const std::string& new_station = line.stations[pos];
            std::vector<std::string>::const_iterator it = std::find(old_stations.begin(), old_stations.end(), new_station);
            if (it != old_stations.end()) continue;  // 已有站点跳过

            // 检测是否为改站：被删集合中是否有站位于线路同一位置
            std::string replace_station;
            if (pos < old_stations.size() && removed_set.count(old_stations[pos]))
                replace_station = old_stations[pos];

            bool is_replace = !replace_station.empty();
            if (is_replace) paired_removed.insert(replace_station);

            Logger::instance().info("Line " + std::to_string(id)
                + (is_replace ? " replace: " + replace_station + " → " : " new station: ")
                + new_station);

            // O(1) 站名 → 站 ID
            uint32_t new_station_id = ds.stationNameToId(new_station);
            if (new_station_id == 0) {
                Logger::instance().warn("Station not found: " + new_station);
                continue;
            }

            // 查所有列车（含 ACTIVE 和 PENDING），看哪些经过此线路
            for (const Train& train : ds.getAllTrains()) {
                bool on_this_line = false;
                for (const Stop& stop : train.stops)
                    if (stop.line_id == id) { 
                        on_this_line = true; 
                        break; 
                    }
                if (!on_this_line) continue;

                // 检查此列车是否已包含该站
                bool has_station = false;
                for (const Stop& stop : train.stops)
                    if (stop.station_id == new_station_id) {
                        has_station = true;
                        break;
                    }
                if (has_station) continue;

                Logger::instance().info("Creating " + std::string(is_replace ? "STOP_REPLACE" : "STOP_INSERT")
                    + " for train " + train.id + " station " + new_station);

                // 计算前后站 + 插入位置（一次性算好，下游直接读取）
                std::string prev_name, next_name;
                int prev_dep = 0, next_arr = 0, insert_idx2 = -1;
                uint32_t prev_sid = 0, next_sid = 0;
                {
                    Line* line_ptr = ds.getLine(id);
                    if (line_ptr) {
                        int st_pos = -1;
                        for (size_t li = 0; li < line_ptr->stations.size(); li++)
                            if (line_ptr->stations[li] == new_station) { 
                                st_pos = static_cast<int>(li); 
                                break; 
                            }
                        if (st_pos > 0) prev_name = line_ptr->stations[st_pos - 1];
                        if (st_pos >= 0 && st_pos + 1 < static_cast<int>(line_ptr->stations.size()))
                            next_name = line_ptr->stations[st_pos + 1];
                    }
                    // 分别在列车 stops 中独立查找（不依赖顺序，列车可能与线路反向）
                    int prev_idx = -1, next_idx = -1;
                    for (size_t si = 0; si < train.stops.size(); si++) {
                        Station* s = ds.getStation(train.stops[si].station_id);
                        if (!s) continue;
                        if (!prev_name.empty() && s->name == prev_name && prev_idx < 0)
                            prev_idx = static_cast<int>(si);
                        if (!next_name.empty() && s->name == next_name && next_idx < 0)
                            next_idx = static_cast<int>(si);
                    }
                    // 方向检测：若 next 在 prev 前面，列车与线路反向，交换前后站
                    if (prev_idx >= 0 && next_idx >= 0 && next_idx < prev_idx) {
                        std::swap(prev_name, next_name);
                        std::swap(prev_idx, next_idx);
                    }
                    if (prev_idx >= 0) {
                        prev_dep = train.stops[prev_idx].departure;
                        prev_sid = train.stops[prev_idx].station_id;
                    }
                    if (next_idx >= 0) {
                        next_arr = train.stops[next_idx].arrival;
                        next_sid = train.stops[next_idx].station_id;
                    }
                    // 插入位置：前站之后（新站在线路开头时插入到后站之前）
                    insert_idx2 = (prev_idx >= 0) ? prev_idx + 1
                        : (next_idx >= 0) ? next_idx : -1;
                }

                // 提交补站审批（线路变更全部以 DRAFT 状态创建）
                json payload;
                payload["train_id"] = train.id;
                payload["line_id"] = id;
                payload["station_name"] = new_station;
                payload["action"] = is_replace ? "replace" : "insert";
                payload["line_name"] = ds.getLine(id) ? ds.getLine(id)->name : "";
                payload["insert_index"] = insert_idx2;
                payload["prev_station_name"] = prev_name;
                payload["prev_departure"] = prev_dep;
                payload["prev_station_id"] = prev_sid;
                payload["next_station_name"] = next_name;
                payload["next_arrival"] = next_arr;
                payload["next_station_id"] = next_sid;
                if (is_replace)
                    payload["replace_station_name"] = replace_station;
                ApprovalType a_type = is_replace ? ApprovalType::STOP_REPLACE : ApprovalType::STOP_INSERT;
                std::string aid = ApprovalService::instance().submit(
                    a_type, ctx->user_id, payload.dump(), ApprovalState::DRAFT);
                affected_trains.push_back({{"train_id", train.id}, {"approval_id", aid}});
            }
        }

        // 检测被删站点，为经过该线路且包含该站的列车创建删站审批
        // 有新增站 = 改站/加站 → 跳过 STOP_REMOVE（旧站是被替换的）
        // 无新增站 = 纯删站 → 生成 STOP_REMOVE
        json removed_trains = json::array();
        if (affected_trains.empty()) {
        for (const std::string& old_st : old_stations) {
            std::vector<std::string>::const_iterator it = std::find(line.stations.begin(), line.stations.end(), old_st);
            if (it != line.stations.end()) continue;  // 仍在线路中，跳过
            if (paired_removed.count(old_st)) continue;  // 已配对为改站，跳过

            Logger::instance().info("Line " + std::to_string(id) + " removed station: " + old_st);

            uint32_t old_st_id = ds.stationNameToId(old_st);
            if (old_st_id == 0) {
                Logger::instance().warn("Station not found: " + old_st);
                continue;
            }

            for (const Train& train : ds.getAllTrains()) {
                bool on_this_line = false;
                bool has_station = false;
                for (const Stop& stop : train.stops) {
                    if (stop.line_id == id) on_this_line = true;
                    if (stop.station_id == old_st_id) has_station = true;
                }
                if (!on_this_line || !has_station) continue;

                Logger::instance().info("Creating STOP_REMOVE for train " + train.id + " station " + old_st);

                // 计算前后站信息（用旧线路站点顺序），供前端渲染
                std::string prev_name, next_name;
                uint32_t prev_sid = 0, next_sid = 0;
                {
                    int st_pos = -1;
                    for (size_t li = 0; li < old_stations.size(); li++)
                        if (old_stations[li] == old_st) { st_pos = static_cast<int>(li); break; }
                    if (st_pos > 0) prev_name = old_stations[st_pos - 1];
                    if (st_pos >= 0 && st_pos + 1 < static_cast<int>(old_stations.size()))
                        next_name = old_stations[st_pos + 1];
                    // 在线路当前站中查前后站 city 对应的 station_id（旧站自身已不在新列表中）
                    for (const Stop& stop : train.stops) {
                        Station* s = ds.getStation(stop.station_id);
                        if (!s) continue;
                        if (s->city == prev_name || s->name == prev_name) prev_sid = stop.station_id;
                        if (s->city == next_name || s->name == next_name) next_sid = stop.station_id;
                    }
                }

                json payload;
                payload["train_id"] = train.id;
                payload["line_id"] = id;
                payload["station_name"] = old_st;
                payload["action"] = "remove";
                payload["line_name"] = ds.getLine(id) ? ds.getLine(id)->name : "";
                payload["prev_station_name"] = prev_name;
                payload["prev_station_id"] = prev_sid;
                payload["next_station_name"] = next_name;
                payload["next_station_id"] = next_sid;
                std::string aid = ApprovalService::instance().submit(
                    ApprovalType::STOP_REMOVE, ctx->user_id, payload.dump(), ApprovalState::DRAFT);
                removed_trains.push_back({{"train_id", train.id}, {"approval_id", aid}});
            }
        }
        }  // if (affected_trains.empty())

        json j;
        j["ok"] = true;
        if (!affected_trains.empty())
            j["affected_trains"] = affected_trains;
        if (!removed_trains.empty())
            j["removed_trains"] = removed_trains;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** DELETE /api/admin/lines/{id} — 删除线路 */
    app.Delete(R"(/api/admin/lines/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        std::optional<AuthContext> ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "线路"))
            return;
        if (!DataStore::instance().removeLine(id)) {
            json j; j["ok"] = false; j["error"] = "线路不存在";
            res.set_content(j.dump(), "application/json"); res.status = 404;
            return;
        }
        json j; j["ok"] = true;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    // ── 查看路网（INFRA_ADMIN）──

}
