// logger.h — 线程安全的日志组件，支持控制台和文件双写
#pragma once

#include <string>
#include <mutex>
#include <fstream>

/**
 * Logger 单例 — 同时输出到控制台和文件。
 * 使用方式：Logger::instance().info("Server started on port {}", port);
 */
class Logger {
public:
    /** 获取全局唯一实例 */
    static Logger& instance();

    /** 设置日志文件路径，默认 "data/server.log"。须在首次写日志前调用 */
    void setLogFile(const std::string& file_path);

    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;

    // ── 内部方法 ──
    void write(const std::string& level, const std::string& message);

    // ── 成员 ──
    std::mutex mutex_;
    std::ofstream file_;
};
