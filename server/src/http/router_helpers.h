// routes_helpers.h — 路由共享工具（认证、限流、JSON转换）
#pragma once

#include "config.h"
#include "system/logger.h"
#include "server.h"
#include "utils.h"
#include "system/wal.h"
#include "sys_admin/audit_service.h"
#include "sys_admin/system_config.h"
#include "security/rate_limiter.h"
#include "security/crypto.h"
#include "data/data_store.h"
#include "auth/auth_service.h"
#include "auth/jwt_service.h"
#include "auth/rbac_middleware.h"
#include "passenger/train_query.h"
#include "passenger/order_service.h"
#include "staff/train_manager.h"
#include "approver/approval_service.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <sstream>

using json = nlohmann::json;

/** 从请求中提取客户端 IP */
inline std::string clientIP(const httplib::Request& req) {
    if (req.has_header("X-Forwarded-For"))
        return req.get_header_value("X-Forwarded-For");
    return req.remote_addr;
}

/** Token Bucket 限流检查 */
inline bool checkRateLimit(const std::string& key, int max, double rate,
                           httplib::Response& res) {
    if (!RateLimiter::instance().allow(key, max, rate)) {
        json j;
        j["ok"] = false;
        j["error"] = "请求过于频繁，请稍后再试";
        res.set_content(j.dump(), "application/json");
        res.status = 429;
        return false;
    }
    return true;
}

/** 停站序列 → JSON 数组 */
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

/** JWT 校验 + RBAC 权限检查 */
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

// ── 通用 HTTP 响应工具 ──

/** 返回 JSON 错误响应（400/404/409 等） */
inline void jsonError(httplib::Response& res, const std::string& msg, int status) {
    json j;
    j["ok"] = false;
    j["error"] = msg;
    res.set_content(j.dump(), "application/json");
    res.status = status;
}

/** 返回 400 Bad Request */
inline void badRequest(httplib::Response& res, const std::string& msg) {
    jsonError(res, msg, 400);
}

/** 返回 500 Internal Server Error，日志记录真实错误，客户端只看到通用信息 */
inline void internalError(httplib::Response& res, const std::string& what) {
    Logger::instance().error(what);
    json j;
    j["ok"] = false;
    j["error"] = "Internal server error";
    res.set_content(j.dump(), "application/json");
    res.status = 500;
}

// ── 参数解析工具 ──

/** 安全解析 uint32_t 路径参数，失败返回 400 */
inline bool parseUint32(const std::string& str, uint32_t& out,
                        httplib::Response& res, const std::string& label) {
    try {
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

/** 解析逗号分隔的 uint32_t 列表 */
inline std::vector<uint32_t> parseIds(const std::string& s) {
    std::vector<uint32_t> ids;
    size_t start = 0, end;
    while ((end = s.find(',', start)) != std::string::npos) {
        ids.push_back(std::stoul(s.substr(start, end - start)));
        start = end + 1;
    }
    ids.push_back(std::stoul(s.substr(start)));
    return ids;
}
