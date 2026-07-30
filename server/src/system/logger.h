// logger.h — 线程安全的日志组件，支持控制台和文件双写
#pragma once

#include <string>
#include <mutex>
#include <fstream>

/**
 * Logger 单例 — 同时输出到控制台和文件。
 * 日志文件路径由 config::SERVER_LOG_FILE 定义，首次写日志时自动创建目录并打开。
 * 使用方式：Logger::instance().info("Server started");
 */
class Logger {
public:
    /** 获取全局唯一实例 */
    static Logger& instance();

    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;

    /** 确保日志文件已打开（惰性初始化，首次调用 write 时触发） */
    void ensureFileOpen();

    // ── 内部方法 ──
    void write(const std::string& level, const std::string& message);

    // ── 成员 ──
    std::mutex mutex_;
    std::ofstream file_;
    bool file_opened_ = false;
};
