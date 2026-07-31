// Auto-split from router_admin.cpp
#include "router_helpers.h"
#include "infra_admin/station_service.h"
#include "infra_admin/line_service.h"
#include <unordered_set>

namespace {

/** 校验站名：非空且不与其他站点重复。excludeId=0 新增，非0 更新（排除自身）。
 *  返回空字符串 = 通过，非空 = 错误信息 */
std::string checkStationName(const std::string& name, uint32_t excludeId) {
    if (name.empty())
        return "站名不能为空";
    for (const auto& s : station_service::getAll())
        if (s.id != excludeId && s.name == name)
            return "站名已存在: " + name;
    return "";
}

/** 校验线路请求体：名称、车站数、时速、名称重复、车站存在。
 *  excludeId=0 新增，非0 更新（排除自身）。
 *  返回空字符串 = 通过，非空 = 错误信息 */
std::string checkLineBody(const json& body, uint32_t excludeId) {
    std::string name = body.value("name", "");
    if (name.empty())
        return "线路名称不能为空";
    if (!body.contains("stations") || !body["stations"].is_array() || body["stations"].size() < 2)
        return "至少需要起点和终点两个站点";
    if (body.value("max_speed_kmh", 0) <= 0)
        return "设计时速必须大于0";
    for (const auto& l : line_service::getAll())
        if (l.id != excludeId && l.name == name)
            return "线路名称已存在: " + name;
    // 车站存在性检查：用 hash set 将 O(S×L) 降为 O(S+L)
    std::unordered_set<std::string> valid;
    for (const auto& reg : station_service::getAll()) {
        valid.insert(reg.name);
        valid.insert(reg.city);
    }
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

    app.Get("/api/admin/lines", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        json j;
        j["ok"] = true;
        j["data"] = line_service::getAll();
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        internalError(res, e.what());
    }
    });

    app.Post("/api/admin/lines", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        json body = json::parse(req.body);

        std::string err = checkLineBody(body, 0);
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

    app.Put(R"(/api/admin/lines/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_LINES);
        if (!ctx) return;
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "线路"))
            return;
        json body = json::parse(req.body);

        std::string err = checkLineBody(body, id);
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
        if (!line_service::update(id, line)) {
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
