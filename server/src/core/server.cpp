// server.cpp — RailwayServer 实现
#include "core/server.h"
#include "core/logger.h"

#include <thread>
#include <chrono>

RailwayServer::RailwayServer() = default;

RailwayServer::~RailwayServer() {
    if (running_)
        stop();
}

void RailwayServer::start(int port) {
    if (running_) {
        Logger::instance().warn("Server is already running");
        return;
    }

    running_ = true;

    // 在独立线程中启动 HTTP 监听
    // httplib::Server::listen() 是阻塞调用，放在独立线程中方便主线程处理信号
    // 绑定 0.0.0.0 而非 127.0.0.1——WSL2 环境下 Windows 浏览器需要通过虚拟网卡访问
    thread_ = std::make_unique<std::thread>([this, port]() {
        Logger::instance().info("Server listening on http://0.0.0.0:" + std::to_string(port));
        app_.listen("0.0.0.0", port);
        Logger::instance().info("Server stopped listening");
    });

    // 给 httplib 一点时间绑定端口
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void RailwayServer::stop() {
    if (!running_)
        return;

    Logger::instance().info("Shutting down server...");
    app_.stop();  // 通知 httplib 停止 accept 循环

    if (thread_ && thread_->joinable()) {
        thread_->join();  // 等待 IO 线程退出
    }

    running_ = false;
    Logger::instance().info("Server stopped");
}
