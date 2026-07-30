// routes.cpp — 路由注册入口，按模块分发到 routes_*.cpp
#include "router.h"
#include "router_helpers.h"

// 各模块路由注册函数
void registerAuthRoutes(RailwayServer& server);
void registerPassengerRoutes(RailwayServer& server);
void registerStaffRoutes(RailwayServer& server);
void registerApprovalRoutes(RailwayServer& server);
void registerSysAdminRoutes(RailwayServer& server);
void registerInfraAdminRoutes(RailwayServer& server);

void registerRoutes(RailwayServer& server) {
    auto& app = server.getApp();

    // 静态文件托管
    app.set_mount_point(config::STATIC_MOUNT_POINT, config::FRONTEND_DIR);

    // 调试端点
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

    // 按模块注册
    registerAuthRoutes(server);
    registerPassengerRoutes(server);
    registerStaffRoutes(server);
    registerApprovalRoutes(server);
    registerSysAdminRoutes(server);
    registerInfraAdminRoutes(server);
}
