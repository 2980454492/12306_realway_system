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

/** 站点种子数据 */
constexpr const char* STATIONS_FILE = "config/stations.json";
/** 线路种子数据 */
constexpr const char* LINES_FILE    = "config/lines.json";
/** 列车种子数据 */
constexpr const char* TRAINS_FILE   = "config/trains.json";
/** 用户账户数据 */
constexpr const char* USERS_FILE    = "config/users.json";

// ── 运行时缓存 / 索引文件（data/ 目录下）──

/** 审批记录 */
constexpr const char* APPROVALS_FILE           = "data/approvals.json";
/** 订单记录 */
constexpr const char* ORDERS_FILE              = "data/orders.json";
/** 服务运行日志 */
constexpr const char* SERVER_LOG_FILE          = "data/server.log";
/** WAL 预写日志 */
constexpr const char* WAL_FILE                 = "data/wal.log";
/** 审计日志（链式 SHA256） */
constexpr const char* AUDIT_LOG_FILE           = "data/audit.log";
/** 系统配置（票价倍率、退票费率） */
constexpr const char* SYSTEM_CONFIG_FILE       = "config/system.json";
/** AES-256-GCM 加密密钥 */
constexpr const char* CRYPTO_KEY_FILE          = "config/key.bin";

}  // namespace config
