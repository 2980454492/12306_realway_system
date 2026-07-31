// Auto-split from router_admin.cpp
#include "router_helpers.h"
#include "infra_admin/station_service.h"
#include "infra_admin/line_service.h"

namespace {

/** 返回 400 Bad Request + JSON 错误信息 */
void badRequest(httplib::Response& res, const std::string& msg) {
    json j;
    j["ok"] = false;
    j["error"] = msg;
    res.set_content(j.dump(), "application/json");
    res.status = 400;
}

/** 从路径参数中安全解析 uint32_t，失败时返回 400 并返回 false */
bool parseUint32(const std::string& str, uint32_t& out, httplib::Response& res, const std::string& label) {
    try {
        // std::stoul 会去掉前导空格，但不能有非数字后缀，stoi 已足够
        int64_t v = std::stoll(str);
        if (v < 0) {
            badRequest(res, "无效的" + label + "ID");
            return false;
        }
        out = static_cast<uint32_t>(v);
        return true;
    } catch (const std::exception&) {
        badRequest(res, "无效的" + label + "ID");
        return false;
    }
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
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    app.Post("/api/admin/stations", [](const httplib::Request& req, httplib::Response& res) {
    try {
        auto ctx = checkAuth(req, res, Permission::MANAGE_STATIONS);
        if (!ctx) return;
        json body = json::parse(req.body);

        // 校验：站名不能为空
        std::string name = body.value("name", "");
        if (name.empty()) {
            badRequest(res, "站名不能为空");
            return;
        }
        // 校验：站名不能重复
        for (const auto& s : station_service::getAll())
            if (s.name == name) {
                badRequest(res, "站名已存在: " + name);
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
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
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

        // 校验：站名不能为空
        std::string name = body.value("name", "");
        if (name.empty()) {
            badRequest(res, "站名不能为空");
            return;
        }
        // 校验：站名不能与其他站点重复
        for (const auto& s : station_service::getAll())
            if (s.id != id && s.name == name) {
                badRequest(res, "站名已存在: " + name);
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
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
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
        j["data"] = line_service::getAll();
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

        // ── 校验 ──

        // 1. 线路名称不能为空
        std::string line_name = body.value("name", "");
        if (line_name.empty()) {
            badRequest(res, "线路名称不能为空");
            return;
        }

        // 2. 途经车站至少 2 个
        if (!body.contains("stations") || !body["stations"].is_array() || body["stations"].size() < 2) {
            badRequest(res, "至少需要起点和终点两个站点");
            return;
        }

        // 3. 设计时速必须大于 0
        int max_speed = body.value("max_speed_kmh", 0);
        if (max_speed <= 0) {
            badRequest(res, "设计时速必须大于0");
            return;
        }

        // 4. 线路名称不能重复
        for (const auto& l : line_service::getAll())
            if (l.name == line_name) {
                badRequest(res, "线路名称已存在: " + line_name);
                return;
            }

        // 5. 每个途经车站必须在系统中已注册（按站名或城市名匹配）
        auto all_stations = station_service::getAll();
        for (const auto& s : body["stations"]) {
            std::string st = s.get<std::string>();
            bool found = false;
            for (const auto& reg : all_stations) {
                if (reg.name == st || reg.city == st) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                badRequest(res, "车站不存在: " + st);
                return;
            }
        }

        Line line;
        line.id = body.value("id", 0);
        line.name = line_name;
        line.type = body.value("type", LineType::NORMAL);
        line.max_speed_kmh = max_speed;
        for (const auto& s : body["stations"])
            line.stations.push_back(s.get<std::string>());
        auto l = line_service::add(line);
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
        uint32_t id;
        if (!parseUint32(req.matches[1], id, res, "线路"))
            return;
        json body = json::parse(req.body);

        // ── 校验（与新增相同，但名称重复检查排除自身）──

        std::string line_name = body.value("name", "");
        if (line_name.empty()) {
            badRequest(res, "线路名称不能为空");
            return;
        }

        if (!body.contains("stations") || !body["stations"].is_array() || body["stations"].size() < 2) {
            badRequest(res, "至少需要起点和终点两个站点");
            return;
        }

        int max_speed = body.value("max_speed_kmh", 0);
        if (max_speed <= 0) {
            badRequest(res, "设计时速必须大于0");
            return;
        }

        for (const auto& l : line_service::getAll())
            if (l.id != id && l.name == line_name) {
                badRequest(res, "线路名称已存在: " + line_name);
                return;
            }

        auto all_stations = station_service::getAll();
        for (const auto& s : body["stations"]) {
            std::string st = s.get<std::string>();
            bool found = false;
            for (const auto& reg : all_stations) {
                if (reg.name == st || reg.city == st) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                badRequest(res, "车站不存在: " + st);
                return;
            }
        }

        Line line;
        line.id = body.value("id", 0);
        line.name = line_name;
        line.type = body.value("type", LineType::NORMAL);
        line.max_speed_kmh = max_speed;
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
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
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
        json j; j["ok"] = false; j["error"] = e.what();
        res.set_content(j.dump(), "application/json"); res.status = 500;
    }
    });

    // ── 查看路网（INFRA_ADMIN）──

}
