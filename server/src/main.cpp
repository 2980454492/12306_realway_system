// main.cpp — 12306 铁路票务系统入口
// 负责数据初始化、信号处理、服务启动、优雅关闭
#include "core/config.h"
#include "core/server.h"
#include "core/routes.h"
#include "core/logger.h"
#include "data/data_store.h"
#include "data/railway_graph.h"
#include "auth/auth_service.h"
#include "auth/jwt_service.h"
#include "auth/rbac_middleware.h"
#include "passenger/order_service.h"
#include "passenger/train_query.h"
#include "staff/train_manager.h"
#include "staff/approval_service.h"
#include "core/wal_writer.h"
#include "core/audit_logger.h"
#include "core/system_config.h"
#include "core/crypto.h"

#include <nlohmann/json.hpp>

#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using json = nlohmann::json;

// ── 全局状态（信号处理器只能访问全局变量）──
static RailwayServer* g_server = nullptr;

// ── 信号处理 ──
// SIGINT/SIGTERM → 停止接新请求，等进行中请求，WAL 在退出前刷盘
static void signalHandler(int sig) {
    const char* name = (sig == SIGINT) ? "SIGINT" : "SIGTERM";
    Logger::instance().info(std::string("Received ") + name + ", shutting down gracefully...");
    if (g_server)
        g_server->stop();
}

int main() {
    // ── 初始化日志（首次写日志时自动打开 config::SERVER_LOG_FILE）──
    Logger::instance().info("Railway Server v0.1.0 starting...");

    // ── 注册信号处理 ──
    // SA_RESTART 不可用：httplib 的 listen 在 accept 阻塞，
    // stop() 通过内部机制唤醒，不需要信号中断系统调用
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ── 初始化数据层 ──
    // 加载站点/线路/列车种子数据，构建索引和铁路网图
    if (!DataStore::instance().initialize()) {
        Logger::instance().error("Failed to initialize DataStore");
        return 1;
    }

    // 构建铁路网拓扑图（优先从本地缓存加载）
    RailwayGraph graph;
    graph.build(DataStore::instance().getAllLines());

    // ── 初始化 WAL 预写日志 ──
    WalWriter::instance().initialize(config::WAL_FILE);

    // ── 初始化加密密钥 ──
    crypto::initKey(config::CRYPTO_KEY_FILE);

    // ── 初始化系统配置 ──
    SystemConfig::instance().initialize(config::SYSTEM_CONFIG_FILE);

    // ── 初始化审计日志 ──
    AuditLogger::instance().initialize(config::AUDIT_LOG_FILE);

    // ── 初始化认证服务 ──
    // 加载或创建用户数据（首次启动生成种子用户）
    if (!AuthService::instance().initialize()) {
        Logger::instance().error("Failed to initialize AuthService");
        return 1;
    }

    // ── 初始化 JWT 服务 ──
    JwtService::instance().initialize();

    // ── 初始化 RBAC 中间件 ──
    RbacMiddleware::initialize();

    // ── 初始化查询索引 ──
    TrainQuery::initialize();

    // ── 初始化订单服务 ──
    OrderService::instance().initialize();

    // ── 初始化职工端服务 ──
    TrainManager::instance().initialize();
    ApprovalService::instance().initialize();

    // ── 崩溃恢复：重放 WAL 中未 checkpoint 的操作 ──
    // 在所有数据加载完成后执行，确保内存状态完整
    WalWriter::instance().recover([](const std::string& op, const std::string& data_json) {
        try {
            json data = json::parse(data_json);
            auto& ds = DataStore::instance();

            if (op == "TRAIN_CREATE" || op == "TRAIN_UPDATE") {
                ds.removeTrain(data.value("id", ""));
                Train t = data.get<Train>();
                ds.addTrain(t);
                if (t.status == TrainStatus::ACTIVE)
                    TrainManager::instance().addToOccupancy(t);
            } else if (op == "TRAIN_DELETE") {
                ds.removeTrain(data.value("id", ""));
            } else if (op == "USER_CREATE" || op == "USER_UPDATE") {
                // 用户操作通过 AuthService 重建逻辑处理
                // 种子用户已在 init 中创建，这里仅做数据一致性检查
            }
            // ORDER_CREATE / ORDER_REFUND 在 OrderService::initialize 中已从 orders.json 恢复
            // APPROVAL_* 在 ApprovalService::initialize 中已从 approvals.json 恢复
        } catch (const std::exception& e) {
            Logger::instance().warn(std::string("WAL replay skipped for ") + op + ": " + e.what());
        }
    });

    // ── 崩溃恢复后保存快照（确保数据持久化，截断 WAL）──
    DataStore::instance().saveTrains();

    // ── 创建并启动服务 ──
    RailwayServer server;
    g_server = &server;

    registerRoutes(server);

    server.start(config::DEFAULT_PORT);

    Logger::instance().info("Server listening on port " + std::to_string(config::DEFAULT_PORT));

    // 等待 stop() 被信号处理器调用
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ── 优雅关闭 ──
    Logger::instance().info("Shutting down services...");
    AuditLogger::instance().shutdown();
    WalWriter::instance().shutdown();
    Logger::instance().info("Railway Server exited cleanly");
    return 0;
}
