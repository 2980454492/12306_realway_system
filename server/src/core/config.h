// config.h — 全局配置常量，所有路径、端口、文件名统一在此定义，禁止在业务代码中硬编码
#pragma once

namespace config {

// ── 目录 ──

/** 前端静态文件目录（cpp-httplib 挂载点） */
constexpr const char* FRONTEND_DIR = "frontend";

// ── 服务 ──

/** HTTP 监听端口 */
constexpr int DEFAULT_PORT = 8080;

/** 前端静态文件挂载路径 */
constexpr const char* STATIC_MOUNT_POINT = "/";

// ── 种子数据文件（config/ 目录下）──

constexpr const char* STATIONS_FILE = "config/stations.json";
constexpr const char* LINES_FILE    = "config/lines.json";
constexpr const char* TRAINS_FILE   = "config/trains.json";
constexpr const char* USERS_FILE    = "config/users.json";

// ── 运行时缓存 / 索引文件（data/ 目录下）──

constexpr const char* RAILWAY_GRAPH_FILE      = "data/railway_graph.json";
constexpr const char* STATION_TRAIN_INDEX_FILE = "data/station_train_index.json";
constexpr const char* STATION_LINE_INDEX_FILE  = "data/station_line_index.json";
constexpr const char* APPROVALS_FILE           = "data/approvals.json";
constexpr const char* ORDERS_FILE              = "data/orders.json";
constexpr const char* SERVER_LOG_FILE          = "data/server.log";

}  // namespace config
