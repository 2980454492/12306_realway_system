// server.h — RailwayServer 封装，管理 HTTP 服务生命周期
#pragma once

#include "httplib.h"

#include <string>
#include <memory>
#include <thread>
#include <atomic>

/**
 * RailwayServer — 对 httplib::Server 的封装。
 * 提供 start/stop 生命周期管理，后续可扩展 SSL、线程池配置。
 * 换框架时只需改本类实现，路由逻辑不用动。
 */
class RailwayServer {
public:
    RailwayServer();
    ~RailwayServer();

    RailwayServer(const RailwayServer&) = delete;
    RailwayServer& operator=(const RailwayServer&) = delete;

    /** 启动 HTTP 监听。阻塞直到 stop() 被调用 */
    void start(int port = 8080);

    /** 优雅关闭：停止接受新请求，等待进行中请求完成 */
    void stop();

    /** 获取底层 httplib::Server 引用，用于注册路由 */
    httplib::Server& getApp() { return app_; }

    /** 运行状态 */
    bool isRunning() const { return running_.load(); }

private:
    httplib::Server app_;
    std::unique_ptr<std::thread> thread_;  // 服务线程
    std::atomic<bool> running_{false};
};
