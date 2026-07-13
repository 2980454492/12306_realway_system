// logger.cpp — Logger 单例实现
#include "core/logger.h"
#include "core/config.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <filesystem>

namespace {
// 生成精确到毫秒的时间戳字符串
std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    std::tm tm_now{};
    localtime_r(&time_t_now, &tm_now);  // 线程安全版本

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}
}  // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::ensureFileOpen() {
    // 调用方已持有 mutex_
    if (file_opened_) return;
    namespace fs = std::filesystem;
    fs::path path(config::SERVER_LOG_FILE);
    if (auto parent = path.parent_path(); !parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent);
    }
    file_.open(config::SERVER_LOG_FILE, std::ios::app);
    file_opened_ = true;
}

void Logger::info(const std::string& message) {
    write("INFO", message);
}

void Logger::warn(const std::string& message) {
    write("WARN", message);
}

void Logger::error(const std::string& message) {
    write("ERROR", message);
}

// ── 内部实现 ──

void Logger::write(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string line = "[" + timestamp() + "] [" + level + "] " + message;

    // 控制台输出：ERROR → stderr，其余 → stdout
    if (level == "ERROR") {
        std::cerr << line << std::endl;
    } else {
        std::cout << line << std::endl;
    }

    // 文件输出（首次调用时惰性打开）
    if (!file_opened_) ensureFileOpen();
    if (file_.is_open()) {
        file_ << line << std::endl;
        file_.flush();  // 每条日志立即落盘，崩溃不丢
    }
}
