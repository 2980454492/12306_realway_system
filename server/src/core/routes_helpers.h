// routes_helpers.h — 路由共享工具（认证、限流、JSON转换）
#pragma once

#include "core/config.h"
#include "core/logger.h"
#include "core/server.h"
#include "core/utils.h"
#include "core/wal_writer.h"
#include "core/audit_logger.h"
#include "core/system_config.h"
#include "core/rate_limiter.h"
#include "core/crypto.h"
#include "data/data_store.h"
#include "auth/auth_service.h"
#include "auth/jwt_service.h"
#include "auth/rbac_middleware.h"
#include "passenger/train_query.h"
#include "passenger/order_service.h"
#include "staff/train_manager.h"
#include "staff/approval_service.h"

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
