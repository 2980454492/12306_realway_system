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
#include "staff/train_manager.h"
#include "staff/approval_service.h"

#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

// ── 全局状态（信号处理器只能访问全局变量）──
static RailwayServer* g_server = nullptr;  // 信号处理期间访问

// ── 信号处理 ──
// SIGINT/SIGTERM → 优雅关闭服务
static void signalHandler(int sig) {
    const char* name = (sig == SIGINT) ? "SIGINT" : "SIGTERM";
    Logger::instance().info(std::string("Received ") + name + ", shutting down...");
    if (g_server) {
        g_server->stop();
    }
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

    // ── 初始化认证服务 ──
    // 加载或创建用户数据（首次启动生成 admin/staff/passenger 种子用户）
    if (!AuthService::instance().initialize()) {
        Logger::instance().error("Failed to initialize AuthService");
        return 1;
    }

    // ── 初始化 JWT 服务 ──
    // 密钥为空则随机生成（重启后旧 Token 全部失效）
    JwtService::instance().initialize();

    // ── 初始化 RBAC 中间件 ──
    RbacMiddleware::initialize();

    // ── 初始化订单服务（加载持久化订单）──
    OrderService::instance().initialize();

    // ── 初始化职工端服务（列车管理 + 审批）──
    TrainManager::instance().initialize();
    ApprovalService::instance().initialize();

    // ── 创建并启动服务 ──
    RailwayServer server;
    g_server = &server;

    registerRoutes(server);

    server.start(config::DEFAULT_PORT);

    // start() 在线程中阻塞，这里等它退出
    // 实际上 signalHandler 调用 stop() 后，start 线程退出，然后这里可以继续
    // 但由于 start() 是异步的，主线程需要一个等待点
    // 使用忙等待检测 running 状态（简单可靠）
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Logger::instance().info("Railway Server exited cleanly");
    return 0;
}
