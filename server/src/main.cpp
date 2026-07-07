// main.cpp — 12306 铁路票务系统入口
// 负责信号处理、服务启动、优雅关闭
#include "server.h"
#include "routes.h"
#include "logger.h"

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
    // ── 初始化日志 ──
    Logger::instance().setLogFile("data/server.log");
    Logger::instance().info("Railway Server v0.1.0 starting...");

    // ── 注册信号处理 ──
    // SA_RESTART 不可用：httplib 的 listen 在 accept 阻塞，
    // stop() 通过内部机制唤醒，不需要信号中断系统调用
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ── 创建并启动服务 ──
    RailwayServer server;
    g_server = &server;

    registerRoutes(server);

    int port = 8080;  // 默认端口，与 Dockerfile / README 保持一致
    server.start(port);

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
