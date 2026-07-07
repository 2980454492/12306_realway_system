// routes.cpp — 路由注册实现
#include "routes.h"
#include "server.h"
#include "logger.h"

#include <nlohmann/json.hpp>
#include <chrono>

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

    Logger::instance().info("Routes registered: GET /, GET /health");
}
