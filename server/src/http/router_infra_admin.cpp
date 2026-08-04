// Auto-split from router_admin.cpp
#include "router_helpers.h"
#include "infra_admin/station_service.h"
#include "infra_admin/line_service.h"

namespace {

/** 校验站名：非空且不与其他站点重复。excludeId=0 新增，非0 更新（排除自身）。
 *  返回空字符串 = 通过，非空 = 错误信息 */
std::string checkStationName(const std::string& name, uint32_t excludeId) {
    if (name.empty())
        return "站名不能为空";
    // O(1)：用预建索引 name→id 查重
    uint32_t existId = DataStore::instance().nameToStationId(name);
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
    auto& valid = DataStore::instance().getStationNameSet();
    for (const auto& s : body["stations"]) {
        std::string st = s.get<std::string>();
        if (!valid.count(st))
            return "车站不存在: " + st;
    }
    return "";
}

}  // namespace

void registerInfraAdminRoutes(RailwayServer& server) {
    auto& app = server.getApp();
    // ── 站点管理（INFRA_ADMIN）──

    /** GET /api/admin/stations — 获取全部站点列表 */
    app.Get("/api/admin/stations", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        json j;
        j["ok"] = true;
        j["data"] = station_service::getAll();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** POST /api/admin/stations — 新增站点 */
    app.Post("/api/admin/stations", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
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
        auto s = station_service::add(station);
        json j;
        j["ok"] = true;
        j["data"] = s;
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** PUT /api/admin/stations/{id} — 修改站点 */
    app.Put(R"(/api/admin/stations/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
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
        if (!station_service::update(id, station)) {
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
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "站点"))
            return;
        if (!station_service::remove(id)) {
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
        auto ctx = RbacMiddleware::authenticate(
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
        j["data"] = line_service::getAll();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** POST /api/admin/lines — 新增线路 */
    app.Post("/api/admin/lines", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
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
        for (const auto& s : body["stations"])
            line.stations.push_back(s.get<std::string>());
        auto l = line_service::add(line);
        json j;
        j["ok"] = true;
        j["data"] = l;
        res.set_content(j.dump(), "application/json");
        res.status = 201;
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** PUT /api/admin/lines/{id} — 修改线路 */
    app.Put(R"(/api/admin/lines/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
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

        // 保存旧站点列表用于比较
        std::vector<std::string> old_stations;
        for (const auto& l : line_service::getAll())
            if (l.id == id) { 
                old_stations = l.stations; 
                break; 
            }

        Line line;
        line.id = body.value("id", 0);
        line.name = body.value("name", "");
        line.type = body.value("type", LineType::NORMAL);
        line.max_speed_kmh = body.value("max_speed_kmh", 0);
        for (const auto& s : body["stations"])
            line.stations.push_back(s.get<std::string>());

        if (!line_service::update(id, line)) {
            json j; 
            j["ok"] = false; 
            j["error"] = "线路不存在";
            res.set_content(j.dump(), "application/json"); 
            res.status = 404;
            return;
        }

        // 检测新增站点，为经过该线路的列车创建补站审批
        json affected_trains = json::array();
        auto& ds = DataStore::instance();
        for (const auto& new_st : line.stations) {
            auto it = std::find(old_stations.begin(), old_stations.end(), new_st);
            if (it != old_stations.end()) continue;  // 已有站点跳过

            Logger::instance().info("Line " + std::to_string(id) + " new station: " + new_st);

            // O(1) 城市名 → 站 ID
            uint32_t new_st_id = ds.cityToStationId(new_st);
            if (new_st_id == 0) {
                Logger::instance().warn("Station city not found: " + new_st);
                continue;
            }

            // 查所有列车（含 ACTIVE 和 PENDING），看哪些经过此线路
            for (const auto& train : ds.getAllTrains()) {
                bool on_this_line = false;
                for (const auto& stop : train.stops)
                    if (stop.line_id == id) { 
                        on_this_line = true; 
                        break; 
                    }
                if (!on_this_line) continue;

                // 检查此列车是否已包含该站
                bool has_station = false;
                for (const auto& stop : train.stops)
                    if (stop.station_id == new_st_id) { 
                        has_station = true; 
                        break; 
                    }
                if (has_station) continue;

                Logger::instance().info("Creating STOP_INSERT for train " + train.id + " station " + new_st);

                // 提交补站审批
                json payload;
                payload["train_id"] = train.id;
                payload["line_id"] = id;
                payload["station_city"] = new_st;
                payload["action"] = "insert";
                std::string aid = ApprovalService::instance().submit(
                    ApprovalType::STOP_INSERT, ctx->user_id, payload.dump());
                affected_trains.push_back({{"train_id", train.id}, {"approval_id", aid}});
            }
        }

        json j;
        j["ok"] = true;
        if (!affected_trains.empty())
            j["affected_trains"] = affected_trains;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    /** DELETE /api/admin/lines/{id} — 删除线路 */
    app.Delete(R"(/api/admin/lines/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "线路"))
            return;
        if (!line_service::remove(id)) {
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
